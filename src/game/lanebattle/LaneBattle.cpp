// ---------------------------------------------------------------------------
// LaneBattle.cpp — the rules.
//
// The first slice of the engine's fourth game, and the first one that adds no
// engine features at all. That is the point: prove the loop is a game before
// paying for a camera, a mouse, or artwork.
//
// Two things worth noticing while reading:
//
//   - Nothing here uses CollisionSystem. A lane battler asks "is the nearest
//     enemy ahead of me within reach?", which is a comparison of x positions,
//     not an overlap of shapes. Reaching for the collision system because it
//     exists would be slower and less clear. Later slices may change that,
//     and the benchmark will decide.
//   - Units walk because they have a Velocity and the engine moves anything
//     with one. Stopping to fight is done by setting that velocity to zero;
//     there is no separate "walking" state to keep in sync.
// ---------------------------------------------------------------------------

#include "LaneBattle.h"
#include "Weather.h"
#include "Environment.h"
#include "Art.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "engine/DataFile.h"
#include "engine/Font.h"
#include "engine/Systems.h"
#include "engine/View.h"

using namespace engine;

namespace lanebattle {
namespace {

AudioDevice* audioDevice = nullptr;
TextureCache* textureCache = nullptr;

// Where each side's castle stands, and where its units appear just clear of
// it. All measured against the WORLD, not the window — the window is only how
// much of the world you happen to be looking at.
constexpr float kLeftCastleX = kCastleMargin;
constexpr float kRightCastleX = kWorldWidth - kCastleMargin - kCastleWidth;

constexpr float kLeftSpawnX = kLeftCastleX + kCastleWidth + 4.0f;
constexpr float kRightSpawnX = kRightCastleX - kMaxUnitWidth - 4.0f;

// Clamped so a bad index can never index off the end of the roster. Every read
// of a unit's stats goes through here.
const UnitKind& kindOf(int kind) { return unitKind(kind); }

// --- Sound -----------------------------------------------------------------


void playHit() {
    if (audioDevice) audioDevice->play(Waveform::Noise, 0.0f, 0.04f, 0.07f);
}

void playDeath() {
    if (audioDevice) audioDevice->play(Waveform::Noise, 0.0f, 0.18f, 0.16f);
}

void playCastleHit() {
    if (audioDevice) audioDevice->play(Waveform::Sine, 120.0f, 0.12f, 0.18f);
}

// --- Building blocks -------------------------------------------------------

Entity createRect(World& world, float x, float y, float width, float height,
                  unsigned char r, unsigned char g, unsigned char b,
                  int layer) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{x, y, 0.0f});

    Sprite sprite;
    sprite.width = static_cast<int>(width);
    sprite.height = static_cast<int>(height);
    sprite.r = r;
    sprite.g = g;
    sprite.b = b;
    sprite.layer = layer;
    world.addComponent(entity, sprite);
    return entity;
}

Entity createText(World& world, const std::string& value, int x, int y,
                  int scale, unsigned char r, unsigned char g, unsigned char b,
                  int layer) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{static_cast<float>(x),
                                         static_cast<float>(y), 0.0f});

    Text text{value, scale, r, g, b, 255};
    text.layer = layer;
    text.screenSpace = true;
    world.addComponent(entity, text);
    return entity;
}

Entity createCenteredText(World& world, const std::string& value, int y,
                          int scale, unsigned char r, unsigned char g,
                          unsigned char b, int layer) {
    const int x = (kWindowWidth - textWidth(value, scale)) / 2;
    return createText(world, value, x, y, scale, r, g, b, layer);
}

Entity createBackdrop(World& world) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{0.0f, 0.0f, 0.0f});

    Sprite panel;
    panel.width = kWindowWidth;
    panel.height = kWindowHeight;
    panel.r = 8;
    panel.g = 8;
    panel.b = 16;
    panel.a = 180;
    panel.layer = kOverlayLayer;
    panel.screenSpace = true;
    world.addComponent(entity, panel);
    return entity;
}

// A unit's forward direction: +1 for the left side, -1 for the right.
float facing(bool leftSide) { return leftSide ? 1.0f : -1.0f; }

// Reads "1,1,2,0" into a session's composition.
//
// Entries naming a unit the roster does not have are dropped rather than
// clamped: a stage asking for kind 7 of a three-unit roster has a typo in it,
// and silently substituting the archer would make the stage quietly wrong
// instead of visibly short.
void parseComposition(const char* text, Session& session) {
    int count = 0;
    int value = 0;
    bool haveDigits = false;

    for (const char* cursor = text; ; ++cursor) {
        if (*cursor >= '0' && *cursor <= '9') {
            // Clamped for the same reason the save loader is: a composition
            // comes out of a data file, and a long run of digits would
            // overflow on the way to being rejected. Signed overflow is
            // undefined, so it has to be stopped before it happens rather
            // than caught after.
            if (value < kMaxUnitKinds) value = value * 10 + (*cursor - '0');
            haveDigits = true;
            continue;
        }

        // The hero is filtered out of stage compositions. It is one per battle
        // by rule, and a wave cycle asking for one would field a stream of
        // them — which is a different game, and not the one being built.
        if (haveDigits && count < kMaxComposition && value < unitKindCount() &&
            value != heroKindIndex()) {
            session.composition[count++] = value;
        }
        value = 0;
        haveDigits = false;

        if (*cursor == '\0') break;
    }

    // A stage whose composition is empty or entirely unreadable would leave
    // the opponent unable to field anything at all, which reads as a bug
    // rather than as an easy level.
    if (count == 0) {
        session.composition[count++] = 1;
    }
    session.compositionLength = count;
}

// Copies one stage's settings into the battle about to be fought.
void applyStage(Session& session, int stage) {
    const StageKind& kind = stageKind(stage);
    session.enemyIncome = kind.enemyIncome;
    session.enemyWaveSize = std::max(1, kind.waveSize);
    parseComposition(kind.composition, session);
}

// A colour channel out of a data file, clamped rather than wrapped: 300 should
// read as "as bright as it goes", not as 44.
unsigned char channel(const DataSection& section, const char* key,
                      unsigned char fallback) {
    const float value = section.number(key, static_cast<float>(fallback));
    return static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, value)));
}

// The stick figure that goes over a unit's block. Drawn a shade darker than
// the block so the limbs read against it.
//
// It carries no points yet — animateUnits fills them in on the first frame,
// which keeps the shape of the figure in exactly one place instead of two that
// have to agree.
//
// It draws on top of the block because it is created after it: within a layer
// the renderer sorts by entity id, and ids increase in creation order. That is
// a real guarantee rather than luck, but it is a quiet one, so: creating the
// figure before its unit would put the limbs behind the silhouette.
Entity createFigure(World& world, Entity owner, const Sprite& body) {
    Entity figure = world.createEntity();
    world.addComponent(figure, Transform{0.0f, 0.0f, 0.0f});
    world.addComponent(figure, Figure{owner});

    Polygon lines;
    lines.closed = false;
    lines.r = static_cast<unsigned char>(body.r / 3);
    lines.g = static_cast<unsigned char>(body.g / 3);
    lines.b = static_cast<unsigned char>(body.b / 3);
    lines.layer = kFieldLayer;
    world.addComponent(figure, lines);

    return figure;
}

// --- Combat ----------------------------------------------------------------

// The nearest enemy ahead of `attacker` and within reach, or kInvalidEntity.
//
// "Ahead" and "within reach" are both decided on x alone, because that is what
// a lane is. Castles count as enemies too, which is what lets the same code
// handle a unit reaching the end of the lane.
//
// This is a linear scan over every unit, run once per unit per frame — O(n²)
// overall. With a dozen units that is free. When it stops being free,
// engine_bench will say so and the answer will be a spatial grid; guessing at
// that now would be optimising a number nobody has measured.
// `hitsAir` is passed rather than read from `stats`, because for one unit on
// the field it is not a property of the roster row: the hero's reach comes from
// the path its owner chose. Explicit and unavoidable rather than a defaulted
// parameter, for the reason View.h spells out — a default here would let every
// existing call keep compiling while quietly meaning something else.
Entity findTargetAhead(World& world, Entity attacker, bool leftSide,
                       float attackerX, const UnitKind& stats, bool hitsAir) {
    const float direction = facing(leftSide);
    const float attackerCenter = attackerX + stats.width / 2.0f;

    Entity best = kInvalidEntity;
    float bestGap = stats.range;

    // Only units and castles can be targets, so only those two pools are
    // walked. This used to scan `world.entities()` and reject everything else
    // by component, which was fine while the world held nothing but the
    // fight — and stopped being fine the moment scenery arrived. Fifty hills
    // and tufts, rejected once per unit per frame, made the test suite 60%
    // slower on their own. Iterating the right pool is not an optimisation so
    // much as not deliberately doing extra work.
    auto consider = [&](Entity other) {
        if (other == attacker) return;

        Team* team = world.getComponent<Team>(other);
        if (!team || team->leftSide == leftSide) return;  // friend, or teamless

        if (Unit* targetUnit = world.getComponent<Unit>(other)) {
            // Already dead, and simply not there any more as far as the fight
            // is concerned. A corpse can outlive its death by a frame — a bolt
            // or a shell lands after the dead have been cleared away, so the
            // body waits for the next pass — and while it did, it went on
            // soaking up blows from the units in front of it. Every hit spent
            // on something already killed is a hit the living did not take,
            // which is a real advantage handed to whoever fired last.
            if (targetUnit->health <= 0.0f) return;

            // Altitude. A target in the sky can only be attacked by something
            // that reaches it, however close it happens to be — this is the
            // question distance cannot answer, and the reason the archer
            // stopped being optional. Castles never fly, so they are never
            // excluded here.
            if (kindOf(targetUnit->kind).flying && !hitsAir) return;
        }

        Transform* transform = world.getComponent<Transform>(other);
        Sprite* sprite = world.getComponent<Sprite>(other);
        if (!transform || !sprite) return;

        // Measured between the facing EDGES, not between the Transforms.
        // Transforms sit at the left edge, so comparing them directly makes
        // reach depend on which side you approach from — a unit could be in
        // range of a 70-pixel castle from one side and have to walk through
        // it from the other. Centres decide what counts as "ahead"; half
        // widths turn that into a gap between surfaces.
        const float targetWidth = static_cast<float>(sprite->width);
        const float targetCenter = transform->x + targetWidth / 2.0f;

        const float centreAhead = (targetCenter - attackerCenter) * direction;
        if (centreAhead < 0.0f) return;  // behind us

        const float gap = centreAhead - (stats.width + targetWidth) / 2.0f;
        if (gap > bestGap) return;

        // An exact tie goes to the lower entity id rather than to whichever
        // the pool happened to yield first.
        //
        // These pools are hash maps, so "first" is an implementation detail
        // that differs between standard libraries — the same battle could pick
        // a different target on Linux than on Windows. Nothing currently
        // depends on the choice, which is exactly why it would have been an
        // unpleasant surprise later: the bug would arrive on a platform this
        // machine cannot run, in a test that passes here every time.
        if (gap == bestGap && best != kInvalidEntity && other > best) return;

        bestGap = gap;
        best = other;
    };

    for (auto& entry : world.view<Unit>()) consider(entry.first);
    for (auto& entry : world.view<Castle>()) consider(entry.first);
    return best;
}

// Is a friendly standing in the way?
//
// Without this the whole army occupies one pixel and fights as a single
// enormous unit, which is exactly why massing was unconditionally correct in
// slice 2. Queuing means only the front few are ever in contact, so a tenth
// unit is worth much less than a second one — the diminishing return that
// makes composition matter more than count.
//
// Two details carry all the behaviour:
//
//   - Only STOPPED friendlies block. A column in transit flows freely, so a
//     fast runner can overtake a marching soldier; the queue only forms where
//     the fighting is.
//   - A friendly only blocks you if its reach is no longer than yours. That is
//     what lets a soldier walk past its own archers to reach the enemy, while
//     archers — which stop far short — never obstruct anybody. Without that
//     one comparison, the first archer sent would wall in every melee unit
//     behind it and ranged units would be a trap rather than a support.
bool blockedByFriendly(World& world, Entity mover, bool leftSide, float moverX,
                       const UnitKind& stats) {
    const float direction = facing(leftSide);
    const float moverCenter = moverX + stats.width / 2.0f;

    // Only units can be in the way, so only the unit pool is walked — same
    // reasoning as findTargetAhead above.
    for (auto& [other, otherUnitRef] : world.view<Unit>()) {
        if (other == mover) continue;

        Unit* otherUnit = &otherUnitRef;
        if (otherUnit->health <= 0.0f) continue;  // a corpse is not a queue

        Team* team = world.getComponent<Team>(other);
        if (!team || team->leftSide != leftSide) continue;

        const UnitKind& otherStats = kindOf(otherUnit->kind);

        // Only things in the same lane are in the way. A griffin overhead is
        // not blocking the soldier beneath it, and a queue in the sky is a
        // different queue from the one on the ground.
        if (otherStats.flying != stats.flying) continue;

        if (otherStats.range > stats.range) continue;  // it stops well short of me

        Velocity* velocity = world.getComponent<Velocity>(other);
        if (!velocity || std::fabs(velocity->dx) > 0.01f) continue;  // still moving

        Transform* transform = world.getComponent<Transform>(other);
        if (!transform) continue;

        const float otherCenter = transform->x + otherStats.width / 2.0f;
        const float centreAhead = (otherCenter - moverCenter) * direction;
        if (centreAhead < 0.0f) continue;  // behind me

        // Exactly level: only the OLDER of the two is ahead — the tie-break
        // targeting already uses, lower id first.
        //
        // Without it, two units on the same pixel each counted the other as the
        // one in front, both waited, and neither ever moved again. Units come
        // out of a gate at one fixed x, so it took only a queue backed up to a
        // gate. The swarm found it — thirty frozen at x = 2272.00 while ten
        // ballistas shot them one at a time for five minutes — and it had been
        // in every battle before the swarm existed, building a turret: griffins
        // attacking soldiers at their own castle froze on one spot and killed
        // every soldier who arrived, from where no soldier could reach. "Ground
        // cannot answer the sky" was partly measured against that turret.
        // Fixing it moved no verdict in the campaign; see v3-plan.md.
        if (centreAhead == 0.0f && other > mover) continue;

        const float gap = centreAhead - (stats.width + otherStats.width) / 2.0f;
        if (gap < kRankGap) return true;
    }
    return false;
}

// --- The cannon ------------------------------------------------------------

// Launches a shot that will land on (targetX, targetY) in exactly
// kCannonFlightTime seconds.
//
// The velocity is solved rather than guessed, which is why a click always
// lands where it was clicked:
//
//     x(T) = x0 + vx*T                 ->  vx = (x1 - x0) / T
//     y(T) = y0 + vy*T + g*T*T/2       ->  vy = (y1 - y0)/T - g*T/2
//
// Firing at a fixed speed and letting gravity decide where it lands would be
// less code and a worse game: aiming would be a feel you have to learn instead
// of a decision you get to make, and the interesting choice here is *where* to
// shoot, not whether you can hit it.
Entity fireCannon(World& world, bool leftSide, float fromX, float fromY,
                  float targetX, float targetY) {
    const float flight = kCannonFlightTime;

    Entity shot = world.createEntity();
    world.addComponent(shot, Transform{fromX, fromY, 0.0f});

    // No Velocity, deliberately: this is the one thing in the game that flies
    // by evaluating its own trajectory rather than by being integrated. See the
    // note on Cannonball for why. AngularVelocity stays — the shell tumbling as
    // it goes is decoration, and decoration may drift.
    world.addComponent(shot, AngularVelocity{7.0f});

    Polygon ball;
    ball.points = {Vec2{-4.0f, -4.0f}, Vec2{4.0f, -4.0f}, Vec2{4.0f, 4.0f},
                   Vec2{-4.0f, 4.0f}};
    ball.closed = true;
    if (leftSide) {
        ball.r = 190; ball.g = 220; ball.b = 255;
    } else {
        ball.r = 255; ball.g = 190; ball.b = 170;
    }
    ball.layer = kFieldLayer;
    world.addComponent(shot, ball);

    Cannonball payload;
    payload.leftSide = leftSide;
    payload.startX = fromX;
    payload.startY = fromY;
    payload.velocityX = (targetX - fromX) / flight;
    payload.velocityY =
        (targetY - fromY) / flight - kCannonGravity * flight / 2.0f;
    world.addComponent(shot, payload);

    if (audioDevice) audioDevice->play(Waveform::Square, 90.0f, 0.10f, 0.16f);
    return shot;
}

void spawnShards(World& world, float x, float y, unsigned char r,
                 unsigned char g, unsigned char b) {
    // Six pieces thrown out of a dying unit. Polygon + Velocity + Lifetime,
    // all of which already existed — a death effect needed no engine work.
    for (int i = 0; i < 6; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / 6.0f;

        Entity shard = world.createEntity();
        world.addComponent(shard, Transform{x, y, angle});
        world.addComponent(shard, Velocity{std::cos(angle) * 70.0f,
                                           std::sin(angle) * 70.0f - 30.0f});
        world.addComponent(shard, AngularVelocity{4.0f});

        Polygon piece;
        piece.points = {Vec2{-4.0f, 0.0f}, Vec2{4.0f, 0.0f}};
        piece.closed = false;
        piece.r = r;
        piece.g = g;
        piece.b = b;
        piece.layer = kFieldLayer;
        world.addComponent(shard, piece);

        world.addComponent(shard, Lifetime{0.45f});
        world.addComponent(shard, Shard{});
    }
}

}  // namespace

// --- The roster ------------------------------------------------------------
//
// Global mutable state, which is worth calling out because this project has
// almost none. It is here because the roster is genuinely one thing shared by
// every part of the game, it is written once at startup and read everywhere
// after, and the alternative — threading a roster reference through every
// function that needs a unit's cost — would be worse to read for no gain.
//
// The cost of the choice is that tests must be able to put it back, hence
// resetBalance(), which the test harness calls before every case.
namespace {
std::vector<UnitKind> gUnitKinds(std::begin(kDefaultUnitKinds),
                                 std::end(kDefaultUnitKinds));

// A fixed array rather than a vector, because a file can retune an upgrade but
// never add one — see the note in the header about what a data file is allowed
// to change and why.
UpgradeKind gUpgrades[kUpgradeCount] = {
    kDefaultUpgrades[0], kDefaultUpgrades[1], kDefaultUpgrades[2]};

// Names are matched case-sensitively and exactly. A file naming a unit that
// does not exist is adding one, not misspelling one — there is no way to tell
// the difference, so the friendlier reading wins.
int indexOfName(const std::string& name) {
    for (std::size_t index = 0; index < gUnitKinds.size(); ++index) {
        if (name == gUnitKinds[index].name) return static_cast<int>(index);
    }
    return -1;
}

// Kept alive for the lifetime of the roster: UnitKind::name is a `const char*`
// pointing either at a string literal in the defaults or at one of these.
std::vector<std::unique_ptr<std::string>> gLoadedNames;
}  // namespace

// --- Stages ----------------------------------------------------------------

namespace {
std::vector<StageKind> gStages(std::begin(kDefaultStages),
                               std::end(kDefaultStages));

// Keeps the strings a data file supplies alive: StageKind holds `const char*`
// pointing either at a literal in the defaults or at one of these.
std::vector<std::unique_ptr<std::string>> gStageStrings;
}  // namespace

const StageKind& stageKind(int stage) {
    if (stage < 0) return gStages.front();
    if (static_cast<std::size_t>(stage) >= gStages.size()) return gStages.back();
    return gStages[static_cast<std::size_t>(stage)];
}

int stageCount() { return static_cast<int>(gStages.size()); }

int visibleStageCount() {
    return std::min(stageCount(), kMaxVisibleStages);
}

int stageAt(float screenX, float screenY) {
    if (screenX < kStageX || screenX > kStageX + kStageWidth) return -1;

    // Only the rows that are actually drawn are clickable. Walking every stage
    // instead would hand back an index for a row sitting on top of the
    // instructions, or off the bottom of the window entirely.
    for (int index = 0; index < visibleStageCount(); ++index) {
        const float top = stageTop(index);
        if (screenY >= top && screenY <= top + kStageHeight) return index;
    }
    return -1;
}

// --- Permanent upgrades and saving -----------------------------------------

const PerkKind& perkKind(int perk) {
    if (perk < 0) return kDefaultPerks[0];
    if (perk >= kPerkCount) return kDefaultPerks[kPerkCount - 1];
    return kDefaultPerks[perk];
}

float perkCost(int perk, int owned) {
    const PerkKind& kind = perkKind(perk);
    float cost = kind.baseCost;
    for (int level = 0; level < owned; ++level) cost *= kind.costGrowth;
    return cost;
}

const HeroPathKind& heroPath(int path) {
    if (path < 0) return kDefaultHeroPaths[0];
    if (path >= kHeroPathCount) return kDefaultHeroPaths[kHeroPathCount - 1];
    return kDefaultHeroPaths[path];
}

int heroPathCount() { return kHeroPathCount; }

float heroUpgradeCost(int path, int upgrade, int owned) {
    if (upgrade < 0 || upgrade >= kHeroUpgradesPerPath) return 0.0f;
    const HeroUpgradeKind& kind = heroPath(path).upgrades[upgrade];
    float cost = kind.baseCost;
    for (int level = 0; level < owned; ++level) cost *= kind.costGrowth;
    return cost;
}

float stageReward(int stage, bool firstClear) {
    const float base =
        kStageRewardBase + kStageRewardPerStage * static_cast<float>(stage);
    return firstClear ? base * kFirstClearBonus : base;
}

namespace {
std::string gSavePath;         // a file somebody named explicitly
bool gUsePlayerFolder = false;  // or the real one, asked for on purpose
}  // namespace

void setSavePath(const std::string& path) {
    gSavePath = path;
    gUsePlayerFolder = false;
}

void usePlayerSavePath() {
    gSavePath.clear();
    gUsePlayerFolder = true;
}

// Where the campaign is read from and written to.
//
// The default here used to be the PLAYER'S save file, and everything that did
// not say otherwise got it. That reads as convenient and is a trap: the thing
// most likely to forget is a diagnostic tool, and a diagnostic tool is exactly
// what must never touch a real save. `ui_shots` stages a victory to photograph
// the win screen, which runs the real logic, which calls saveCampaign — so
// taking screenshots quietly banked gold into somebody's campaign. Nothing
// failed; the tests were green; the save was simply different afterwards.
//
// So the dangerous option is now the one you have to ask for. `main.cpp` asks;
// nothing else does. A tool that forgets writes a scratch file next to itself,
// which is the right way round: forgetting should cost a stray file in a build
// folder, not a player's progress.
std::string savePath() {
    if (!gSavePath.empty()) return gSavePath;
    if (gUsePlayerFolder) return userPath("TinyEngine", "LaneBattle", "campaign.txt");
    return "campaign-scratch.txt";
}

bool saveCampaign(const Campaign& campaign) {
    DataWriter writer;
    writer.beginSection("campaign");
    writer.set("stages_unlocked", campaign.stagesUnlocked);
    writer.set("bank", campaign.bank);

    // Written by NAME rather than by index, so reordering the perks in the
    // header cannot silently turn everyone's weapons into ramparts.
    for (int perk = 0; perk < kPerkCount; ++perk) {
        writer.set(std::string("perk_") + perkKind(perk).name,
                   campaign.perks[perk]);
    }

    // The hero's path is written by NAME for the same reason the perks are:
    // an index is a promise that the enum never gets reordered, and this one
    // decides whether somebody's hero can reach the sky.
    writer.set("hero_path", std::string(heroPath(campaign.heroPath).name));
    for (int path = 1; path < kHeroPathCount; ++path) {
        for (int up = 0; up < kHeroUpgradesPerPath; ++up) {
            const char* name = heroPath(path).upgrades[up].name;
            if (!name || name[0] == '\0') continue;
            writer.set(std::string("hero_") + name,
                       campaign.heroUpgrades[path][up]);
        }
    }

    // The loadout and the training, both written by unit NAME.
    //
    // The Campaign holds them by roster index because a data file may add unit
    // types and there is no fixed set of names to key on in memory. A SAVE is
    // different: it outlives the roster that produced it, and an index into a
    // roster that has since gained a row in the middle is a promise nobody
    // made. Names survive that; indices do not.
    // One field per slot, EMPTY ones included — "SOLDIER,,ARCHER," and not
    // "SOLDIER,ARCHER".
    //
    // Skipping the holes packed the names together, and loading filled the
    // slots from the front, so a loadout of [-, SOLDIER, -, ARCHER] came back
    // as [SOLDIER, ARCHER, -, -]. Nothing was lost, which is why it went
    // unnoticed; the units all came back. What moved was the KEY each one
    // answers to, and toggleCarried says in as many words that it leaves a hole
    // rather than shuffling the bar precisely because a player learns those
    // positions. The promise held for the session and broke on the next launch.
    std::string carried;
    for (int slot = 0; slot < kLoadoutSlots; ++slot) {
        if (slot > 0) carried += ",";
        const int kind = campaign.loadout[slot];
        if (kind < 0 || kind >= unitKindCount()) continue;
        carried += unitKind(kind).name;
    }
    writer.set("loadout", carried);

    for (int kind = 0; kind < unitKindCount() && kind < kMaxUnitKinds; ++kind) {
        if (campaign.unitLevels[kind] <= 0) continue;
        writer.set(std::string("level_") + unitKind(kind).name,
                   campaign.unitLevels[kind]);
    }

    std::string cleared;
    for (int stage = 0; stage < kMaxStages; ++stage) {
        if (!campaign.cleared[stage]) continue;
        if (!cleared.empty()) cleared += ",";
        cleared += std::to_string(stage);
    }
    writer.set("cleared", cleared);

    return writer.save(savePath());
}

bool loadCampaign(Campaign& campaign) {
    DataFile file;
    if (!file.load(savePath())) return false;

    const DataSection* section = file.first("campaign");
    if (!section) return false;

    // Clamped on the way in. A save file is a text file a player can edit, and
    // a stages_unlocked of 900 should open the campaign rather than index off
    // the end of the stage list.
    campaign.stagesUnlocked = std::min(
        std::max(1, section->integer("stages_unlocked", 1)), stageCount());
    campaign.bank = std::max(0, section->integer("bank", 0));

    for (int perk = 0; perk < kPerkCount; ++perk) {
        campaign.perks[perk] = std::max(
            0, section->integer(std::string("perk_") + perkKind(perk).name, 0));
    }

    // Matched by name, and an unrecognised one falls back to no path rather
    // than to whatever index it happened to resemble.
    campaign.heroPath = 0;
    const std::string pathName = section->text("hero_path", "NONE");
    for (int path = 0; path < kHeroPathCount; ++path) {
        if (pathName == heroPath(path).name) campaign.heroPath = path;
    }
    for (int path = 0; path < kHeroPathCount; ++path) {
        for (int up = 0; up < kHeroUpgradesPerPath; ++up) {
            const HeroUpgradeKind& kind = heroPath(path).upgrades[up];
            if (!kind.name || kind.name[0] == '\0') {
                campaign.heroUpgrades[path][up] = 0;
                continue;
            }
            // Clamped to the cap on the way in. The cap is what makes points
            // scarce, and a save file is a text file a player can edit.
            const int owned =
                section->integer(std::string("hero_") + kind.name, 0);
            campaign.heroUpgrades[path][up] =
                std::min(std::max(0, owned), kind.maxLevel);
        }
    }

    // Matched by name back to whatever index that name holds now. A name the
    // roster no longer has is simply dropped, which turns "the file was
    // written against a roster that has changed" into an empty slot rather
    // than into somebody else's unit.
    // Position is carried by the comma, not by the order of the names: the nth
    // field is the nth slot, and an empty field is a slot deliberately left
    // open. The slot counter therefore advances on EVERY separator, not only
    // on the ones that named a unit — which is what keeps a hole a hole.
    //
    // A save written before this change has no holes in it to lose, so it
    // still loads to exactly what it used to mean.
    for (int slot = 0; slot < kLoadoutSlots; ++slot) campaign.loadout[slot] = -1;
    {
        const std::string carried = section->text("loadout", "");
        std::string name;
        int slot = 0;
        for (std::size_t at = 0; at <= carried.size(); ++at) {
            const char c = at < carried.size() ? carried[at] : ',';
            if (c != ',') {
                name += c;
                continue;
            }
            if (!name.empty() && slot < kLoadoutSlots) {
                for (int kind = 0; kind < unitKindCount(); ++kind) {
                    if (name == unitKind(kind).name && kind != heroKindIndex()) {
                        campaign.loadout[slot] = kind;
                        break;
                    }
                }
            }
            ++slot;
            name.clear();
        }
    }

    for (int kind = 0; kind < kMaxUnitKinds; ++kind) campaign.unitLevels[kind] = 0;
    for (int kind = 0; kind < unitKindCount() && kind < kMaxUnitKinds; ++kind) {
        const int level =
            section->integer(std::string("level_") + unitKind(kind).name, 0);
        campaign.unitLevels[kind] =
            std::min(std::max(0, level), kMaxUnitLevel);
    }

    for (int stage = 0; stage < kMaxStages; ++stage) {
        campaign.cleared[stage] = false;
    }
    const std::string cleared = section->text("cleared", "");
    int value = 0;
    bool digits = false;
    for (std::size_t at = 0; at <= cleared.size(); ++at) {
        const char c = at < cleared.size() ? cleared[at] : ',';
        if (c >= '0' && c <= '9') {
            // Stopped rather than allowed to wrap. A save is a text file a
            // player can edit, and a run of twenty digits would overflow a
            // signed int on the way to being rejected — which is undefined
            // behaviour, not a rejection. Clamping keeps the value nonsense
            // and keeps it defined, and the `< kMaxStages` test below still
            // throws it away.
            if (value < kMaxStages) value = value * 10 + (c - '0');
            digits = true;
            continue;
        }
        if (digits && value < kMaxStages) campaign.cleared[value] = true;
        value = 0;
        digits = false;
    }
    return true;
}

const SpellKind& spellKind(int spell) {
    if (spell < 0) return kDefaultSpells[0];
    if (spell >= kSpellCount) return kDefaultSpells[kSpellCount - 1];
    return kDefaultSpells[spell];
}

int spellAt(float screenX, float screenY) {
    if (screenX < kSpellX || screenX > kSpellX + kSpellWidth) return -1;

    for (int index = 0; index < kSpellCount; ++index) {
        const float top = spellTop(index);
        if (screenY >= top && screenY <= top + kSpellHeight) return index;
    }
    return -1;
}

int pathAt(float screenX, float screenY) {
    if (screenY < kPathY || screenY > kPathY + kPathHeight) return -1;

    // Numbered from 1: None is a state, not an offer.
    for (int path = 1; path < kHeroPathCount; ++path) {
        const float left = pathLeft(path - 1);
        if (screenX >= left && screenX <= left + kPathWidth) return path;
    }
    return -1;
}

int heroUpgradeAt(float screenX, float screenY) {
    if (screenX < kHeroUpgradeX || screenX > kHeroUpgradeX + kHeroUpgradeWidth) {
        return -1;
    }
    for (int index = 0; index < kHeroUpgradesPerPath; ++index) {
        const float top = heroUpgradeTop(index);
        if (screenY >= top && screenY <= top + kHeroUpgradeHeight) return index;
    }
    return -1;
}

int armyRowAt(float screenX, float screenY) {
    if (screenX < kArmyX || screenX > kArmyX + kArmyWidth) return -1;

    const int rows = std::min(sellableKindCount(), kMaxVisibleArmyRows);
    for (int index = 0; index < rows; ++index) {
        const float top = armyTop(index);
        if (screenY >= top && screenY <= top + kArmyHeight) return index;
    }
    return -1;
}

bool armyTrainHit(float screenX, float screenY) {
    return armyRowAt(screenX, screenY) >= 0 && screenX >= kTrainX;
}

int perkAt(float screenX, float screenY) {
    if (screenX < kPerkX || screenX > kPerkX + kPerkWidth) return -1;

    for (int index = 0; index < kPerkCount; ++index) {
        const float top = perkTop(index);
        if (screenY >= top && screenY <= top + kPerkHeight) return index;
    }
    return -1;
}

Campaign& campaignOf(World& world) {
    for (auto& entry : world.view<Campaign>()) return entry.second;

    // Created on first use rather than by whichever scene happens to run
    // first. The title screen, the stage list and a test that starts a battle
    // directly all need it, and none of them should have to own it.
    const Entity entity = world.createEntity();
    world.addComponent(entity, Campaign{});
    return *world.getComponent<Campaign>(entity);
}

const UnitKind& unitKind(int kind) {
    if (kind < 0) return gUnitKinds.front();
    if (static_cast<std::size_t>(kind) >= gUnitKinds.size()) {
        return gUnitKinds.back();
    }
    return gUnitKinds[static_cast<std::size_t>(kind)];
}

int unitKindCount() { return static_cast<int>(gUnitKinds.size()); }

void resetBalance() {
    gUnitKinds.assign(std::begin(kDefaultUnitKinds), std::end(kDefaultUnitKinds));
    gLoadedNames.clear();
    gStages.assign(std::begin(kDefaultStages), std::end(kDefaultStages));
    gStageStrings.clear();
    for (int index = 0; index < kUpgradeCount; ++index) {
        gUpgrades[index] = kDefaultUpgrades[index];
    }
}

bool loadBalance(const std::string& path) {
    DataFile file;
    if (!file.load(path)) return false;  // nothing found; defaults stand

    for (const DataSection* section : file.all("unit")) {
        const std::string name = section->text("name", "");
        if (name.empty()) continue;  // a row with no name names nothing

        int index = indexOfName(name);
        if (index < 0 && gUnitKinds.size() >= static_cast<std::size_t>(kMaxUnitKinds)) {
            // The roster is full. Each side stores one cooldown timer per kind
            // on its Session, so the ceiling is real rather than arbitrary —
            // and silently growing past it would corrupt nothing visibly, just
            // stop the extra units from ever being spawnable.
            continue;
        }
        if (index < 0) {
            // A new kind. Its defaults are the soldier's, so a row that sets
            // only a cost still produces something that can walk and fight
            // rather than a unit with zero health that dies on arrival.
            gLoadedNames.push_back(std::make_unique<std::string>(name));
            UnitKind added = kDefaultUnitKinds[1];
            added.name = gLoadedNames.back()->c_str();
            gUnitKinds.push_back(added);
            index = static_cast<int>(gUnitKinds.size()) - 1;
        }

        // Every field falls back to what the row already held, so a file may
        // change one number and leave the rest alone.
        UnitKind& kind = gUnitKinds[static_cast<std::size_t>(index)];
        kind.cost = section->number("cost", kind.cost);
        kind.health = section->number("health", kind.health);
        kind.damage = section->number("damage", kind.damage);
        kind.range = section->number("range", kind.range);
        kind.attackDelay = section->number("attack_delay", kind.attackDelay);
        kind.speed = section->number("speed", kind.speed);
        kind.cooldown = section->number("cooldown", kind.cooldown);
        kind.flying = section->integer("flying", kind.flying ? 1 : 0) != 0;
        kind.hitsAir = section->integer("hits_air", kind.hitsAir ? 1 : 0) != 0;
        kind.width = section->number("width", kind.width);
        kind.height = section->number("height", kind.height);

        kind.leftR = channel(*section, "left_r", kind.leftR);
        kind.leftG = channel(*section, "left_g", kind.leftG);
        kind.leftB = channel(*section, "left_b", kind.leftB);
        kind.rightR = channel(*section, "right_r", kind.rightR);
        kind.rightG = channel(*section, "right_g", kind.rightG);
        kind.rightB = channel(*section, "right_b", kind.rightB);

        // Artwork. Naming a sheet is what turns a unit from a coloured block
        // into frame animation, and it is the only field here that needs its
        // string to outlive the DataFile — hence the same owned-strings list
        // the unit names use.
        if (section->has("sheet")) {
            gLoadedNames.push_back(
                std::make_unique<std::string>(section->text("sheet", "")));
            const std::string& owned = *gLoadedNames.back();
            kind.sheet = owned.empty() ? nullptr : owned.c_str();
        }
        kind.frameWidth = section->integer("frame_width", kind.frameWidth);
        kind.frameHeight = section->integer("frame_height", kind.frameHeight);
        kind.frameCount = section->integer("frame_count", kind.frameCount);
        kind.frameSeconds = section->number("frame_seconds", kind.frameSeconds);

        // The generated art. Strings are kept alive the same way `sheet` is:
        // the roster row holds a plain pointer, so something must own the text.
        auto keep = [&](const char* key, const char* current) -> const char* {
            if (!section->has(key)) return current;
            gLoadedNames.push_back(
                std::make_unique<std::string>(section->text(key, "")));
            const std::string& owned = *gLoadedNames.back();
            return owned.empty() ? nullptr : owned.c_str();
        };
        kind.enemySheet = keep("enemy_sheet", kind.enemySheet);
        kind.blow = keep("blow", kind.blow);
        kind.artHeight =
            std::clamp(section->number("art_height", kind.artHeight), 0.0f, 400.0f);
        // One key per hero path, named after the path: sheet_warden and so on.
        // Derived from the path table rather than listed, so a renamed path
        // cannot quietly stop being read.
        for (int heroPathIndex = 1; heroPathIndex < kHeroPathCount; ++heroPathIndex) {
            std::string key = "sheet_";
            for (const char* c = heroPath(heroPathIndex).name; *c; ++c) {
                key += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
            }
            kind.pathSheet[heroPathIndex] = keep(key.c_str(), kind.pathSheet[heroPathIndex]);
        }
    }

    // Stages, unlike units, are REPLACED rather than merged. A campaign is an
    // ordered list, not a set of named things: a file that describes three
    // stages means a three-stage campaign, and quietly leaving the other five
    // behind it would produce a campaign nobody wrote.
    const std::vector<const DataSection*> stages = file.all("stage");
    if (!stages.empty()) {
        gStages.clear();
        gStageStrings.clear();

        for (const DataSection* section : stages) {
            if (gStages.size() >= static_cast<std::size_t>(kMaxStages)) break;

            gStageStrings.push_back(std::make_unique<std::string>(
                section->text("name", "STAGE")));
            const char* name = gStageStrings.back()->c_str();

            gStageStrings.push_back(std::make_unique<std::string>(
                section->text("composition", "1")));
            const char* composition = gStageStrings.back()->c_str();

            StageKind stage;
            stage.name = name;
            stage.composition = composition;
            stage.enemyIncome = section->number("enemy_income", 1.0f);
            stage.enemyCastleHealth =
                section->number("enemy_castle_health", kCastleHealth);
            stage.waveSize = section->integer("wave_size", 3);
            gStages.push_back(stage);
        }

        if (gStages.empty()) {
            gStages.assign(std::begin(kDefaultStages), std::end(kDefaultStages));
        }
    }

    // Upgrades are matched by name against a fixed set rather than appended:
    // each one has its own rule in code, so a file naming an upgrade the game
    // does not have is a typo, not a new feature, and is ignored.
    for (const DataSection* section : file.all("upgrade")) {
        const std::string name = section->text("name", "");
        for (int index = 0; index < kUpgradeCount; ++index) {
            if (name != gUpgrades[index].name) continue;

            gUpgrades[index].baseCost =
                section->number("cost", gUpgrades[index].baseCost);
            gUpgrades[index].costGrowth =
                section->number("growth", gUpgrades[index].costGrowth);
            gUpgrades[index].effect =
                section->number("effect", gUpgrades[index].effect);
            break;
        }
    }
    return true;
}

// --- Queries ---------------------------------------------------------------

Session* findSession(World& world) {
    for (Entity entity : world.entities()) {
        if (Session* session = world.getComponent<Session>(entity)) {
            return session;
        }
    }
    return nullptr;
}

Entity findCastle(World& world, bool leftSide) {
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Castle>(entity)) continue;
        Team* team = world.getComponent<Team>(entity);
        if (team && team->leftSide == leftSide) return entity;
    }
    return kInvalidEntity;
}

int countUnits(World& world, bool leftSide) {
    return countUnitsOfKind(world, leftSide, -1);
}

int countUnitsOfKind(World& world, bool leftSide, int kind) {
    int count = 0;
    // The unit pool, not the whole world. Same reasoning as findTargetAhead:
    // this runs three times a frame against a world that is mostly scenery.
    // A count does not care what order it visits things in, so nothing about
    // determinism changes with the iteration order.
    for (auto& [entity, unit] : world.view<Unit>()) {
        if (kind >= 0 && unit.kind != kind) continue;
        Team* team = world.getComponent<Team>(entity);
        if (team && team->leftSide == leftSide) ++count;
    }
    return count;
}

int countFigures(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Figure>(entity)) ++count;
    }
    return count;
}

void animateUnits(World& world, float dt) {
    for (auto& [entity, figure] : world.view<Figure>()) {
        Unit* unit = world.getComponent<Unit>(figure.owner);
        Team* team = world.getComponent<Team>(figure.owner);
        Transform* body = world.getComponent<Transform>(figure.owner);
        Velocity* velocity = world.getComponent<Velocity>(figure.owner);

        // An orphan. A death already takes its own figure with it, so this is
        // the safety net rather than the mechanism: it catches any future path
        // that makes a unit disappear without going through removeTheDead.
        if (!unit || !team || !body || !velocity) {
            world.destroyLater(entity);
            continue;
        }

        const UnitKind& stats = kindOf(unit->kind);
        const float direction = facing(team->leftSide);

        // The walk cycle advances with DISTANCE, not with time, so a runner's
        // legs move faster than a soldier's without either being told to and
        // nothing ever slides along with its feet still.
        unit->phase += std::fabs(velocity->dx) * dt * kWalkCycleRate;
        unit->swing = std::max(0.0f, unit->swing - dt * kSwingDecayRate);

        // A figure with artwork chooses a row of its sheet instead of bending
        // line segments. Same owner, same orphan rule, same frame.
        if (world.hasComponent<ArtFigure>(entity)) {
            poseArtFigure(world, entity, dt);
            continue;
        }

        Transform* at = world.getComponent<Transform>(entity);
        Polygon* lines = world.getComponent<Polygon>(entity);
        if (!at || !lines) continue;

        // The figure hangs off the bottom centre of the block it decorates.
        // Its own bottom, not the ground line — a griffin's legs belong under
        // the griffin rather than dangling a hundred and fifty pixels below it.
        at->x = body->x + stats.width / 2.0f;
        at->y = body->y + stats.height;

        const float hipY = -stats.height * 0.45f;
        const float shoulderY = -stats.height * 0.78f;
        const float step = std::sin(unit->phase) * kLegSwing;

        // The arm sweeps from raised to lowered as the swing plays out, and
        // rests slightly forward when idle.
        const float armAngle = unit->swing > 0.0f
                                   ? (-1.1f + 1.9f * (1.0f - unit->swing))
                                   : 0.25f;
        const float reach = stats.range > 100.0f ? 10.0f : 16.0f;  // a bow is held closer
        const float handX = std::cos(armAngle) * reach * direction;
        const float handY = shoulderY + std::sin(armAngle) * reach;

        // One unbroken stroke, retracing the hip: foot, hip, other foot, back
        // up through the hip to the shoulder, then out along the arm. Retracing
        // costs one duplicated line and saves needing a second entity.
        lines->points.clear();
        lines->points.push_back(Vec2{step * direction, 0.0f});
        lines->points.push_back(Vec2{0.0f, hipY});
        lines->points.push_back(Vec2{-step * direction, 0.0f});
        lines->points.push_back(Vec2{0.0f, hipY});
        lines->points.push_back(Vec2{0.0f, shoulderY});
        lines->points.push_back(Vec2{handX, handY});
    }
}

// --- Upgrades --------------------------------------------------------------

const UpgradeKind& upgradeKind(int upgrade) {
    if (upgrade < 0) return gUpgrades[0];
    if (upgrade >= kUpgradeCount) return gUpgrades[kUpgradeCount - 1];
    return gUpgrades[upgrade];
}

float upgradeCost(int upgrade, int owned) {
    if (upgrade < 0 || upgrade >= kUpgradeCount) return 0.0f;
    const UpgradeKind& kind = upgradeKind(upgrade);

    float cost = kind.baseCost;
    for (int level = 0; level < owned; ++level) cost *= kind.costGrowth;
    return cost;
}

namespace {
// The upgrade counts belonging to one side. Both sides buy from the same
// table, so everything below reads whichever array applies rather than being
// written twice with the words swapped — which is how slice 1's asymmetry bug
// happened in the first place.
const int* upgradesOf(const Session& session, bool leftSide) {
    return leftSide ? session.upgrades : session.enemyUpgrades;
}
}  // namespace

float goldPerSecondFor(const Session& session, bool leftSide) {
    const int levels = upgradesOf(session, leftSide)[static_cast<int>(Upgrade::Income)];
    // GRANARY is yours alone, and is added rather than multiplied so that its
    // worth does not swing with whatever the stage sets the opponent's rate to.
    const float granary = leftSide ? session.bonusGoldPerSecond : 0.0f;
    return kGoldPerSecond + granary +
           upgradeKind(static_cast<int>(Upgrade::Income)).effect *
               static_cast<float>(levels);
}

int populationCapFor(const Session& session, bool leftSide) {
    const int levels = upgradesOf(session, leftSide)[static_cast<int>(Upgrade::Supply)];
    return kPopulationCap +
           static_cast<int>(upgradeKind(static_cast<int>(Upgrade::Supply)).effect) *
               levels;
}

float castleMaxHealthFor(const Session& session, bool leftSide) {
    const int levels = upgradesOf(session, leftSide)[static_cast<int>(Upgrade::Walls)];
    return kCastleHealth +
           upgradeKind(static_cast<int>(Upgrade::Walls)).effect *
               static_cast<float>(levels);
}

int upgradeAt(float screenX, float screenY) {
    if (screenX < kUpgradeX || screenX > kUpgradeX + kUpgradeWidth) return -1;

    for (int index = 0; index < kUpgradeCount; ++index) {
        const float top = upgradeTop(index);
        if (screenY >= top && screenY <= top + kUpgradeHeight) return index;
    }
    return -1;
}

int countCannonballs(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Cannonball>(entity)) ++count;
    }
    return count;
}

int heroKindIndex() {
    for (int kind = 0; kind < unitKindCount(); ++kind) {
        if (std::string(unitKind(kind).name) == kHeroName) return kind;
    }
    return -1;
}

int sellableKindCount() {
    const int hero = heroKindIndex();
    return unitKindCount() - (hero >= 0 ? 1 : 0);
}

int sellableKind(int index) {
    const int hero = heroKindIndex();
    int seen = 0;
    for (int kind = 0; kind < unitKindCount(); ++kind) {
        if (kind == hero) continue;
        if (seen == index) return kind;
        ++seen;
    }
    return -1;
}

namespace {
int gLoadout[kLoadoutSlots] = {-1, -1, -1, -1};

// Whether anybody has chosen at all.
//
// The fallback to roster order has to be all-or-nothing, and it was not: it
// applied PER SLOT, so a player who deliberately carried three units and left
// the fourth empty got a fourth button selling whatever roster order put
// there. The army screen said CARRYING 3 OF 4 and the spawn bar sold four —
// the two screens disagreeing about the same fact, which is this file's
// oldest recurring bug.
//
// An empty slot is a choice. Only "nobody has chosen anything" falls back, and
// that exists so every test and probe written before loadouts existed still
// means what it meant then.
bool gLoadoutChosen = false;
}  // namespace

void setLoadout(const int* kinds, int count) {
    gLoadoutChosen = false;
    for (int slot = 0; slot < kLoadoutSlots; ++slot) {
        const int wanted = (kinds && slot < count) ? kinds[slot] : -1;
        // Validated on the way in rather than trusted: this comes from a save
        // file, and a roster can shrink between one launch and the next.
        const bool usable =
            wanted >= 0 && wanted < unitKindCount() && wanted != heroKindIndex();
        gLoadout[slot] = usable ? wanted : -1;
        if (usable) gLoadoutChosen = true;
    }
}

void resetLoadout() {
    for (int slot = 0; slot < kLoadoutSlots; ++slot) gLoadout[slot] = -1;
    gLoadoutChosen = false;
}

int loadoutKind(int slot) {
    if (slot < 0 || slot >= kLoadoutSlots) return -1;
    if (gLoadoutChosen) return gLoadout[slot];
    // Nobody has chosen: the first sellable kinds, which is what the bar
    // showed before there was such a thing as a loadout.
    return sellableKind(slot);
}

bool inLoadoutOf(const Campaign& campaign, int kind) {
    if (kind < 0) return false;
    for (int slot = 0; slot < kLoadoutSlots; ++slot) {
        if (campaign.loadout[slot] == kind) return true;
    }
    return false;
}

bool inLoadout(int kind) {
    if (kind < 0) return false;
    for (int slot = 0; slot < kLoadoutSlots; ++slot) {
        if (loadoutKind(slot) == kind) return true;
    }
    return false;
}

// Which unit kind a bar slot sells, or -1 if that slot is empty.
//
// The loadout is the answer now. It used to be "the nth sellable row", which
// meant roster order decided what a player could field and everything past the
// bar's width was unreachable — payable for, never sendable.
//
// The hero is still not on it: it is not bought, and a loadout slot spent on
// something free would be a slot wasted.
int kindForButton(int slot) { return loadoutKind(slot); }

// One past the LAST carried slot — not how many slots are carried.
//
// It was the count, and every caller loops `slot < visibleButtonCount()`, so a
// hole anywhere but the end hid whatever came after it. Drop the RUNNER from
// the front of the default loadout and you carry three units in slots two to
// four; the count said three, the loops stopped at slot three, and the GRIFFIN
// in slot four had no button, no click and no key. Nothing in the game could
// send it. The army screen promises that dropping a unit leaves a hole rather
// than shuffling the bar, so holes are a normal state rather than an edge case
// — and found only because the defeat screen started sending players back to
// that screen, and a screenshot of a soldiers-only army said it had sent none.
//
// Every caller already skips an empty slot inside the span, so the bar draws
// the hole as a gap and its key does nothing, which is what a hole means.
int visibleButtonCount() {
    int span = 0;
    for (int slot = 0; slot < kLoadoutSlots && slot < kMaxVisibleButtons; ++slot) {
        if (loadoutKind(slot) >= 0) span = slot + 1;
    }
    return span;
}

float trainCost(int kind, int owned) {
    float cost = unitKind(kind).cost * kTrainCostFactor;
    // A free unit still has to cost something to train, or the hero — and
    // anything a data file prices at zero — would level for nothing.
    if (cost < 40.0f) cost = 40.0f;
    for (int level = 0; level < owned; ++level) cost *= kTrainCostGrowth;
    return cost;
}

namespace {
int gTrainingLevels[kMaxUnitKinds] = {};
}  // namespace

void setTrainingLevels(const int* levels, int count) {
    for (int kind = 0; kind < kMaxUnitKinds; ++kind) {
        const int wanted = (levels && kind < count) ? levels[kind] : 0;
        gTrainingLevels[kind] = std::min(std::max(0, wanted), kMaxUnitLevel);
    }
}

void resetTraining() {
    for (int kind = 0; kind < kMaxUnitKinds; ++kind) gTrainingLevels[kind] = 0;
}

int trainingLevel(int kind) {
    if (kind < 0 || kind >= kMaxUnitKinds) return 0;
    return gTrainingLevels[kind];
}

float trainedHealth(int kind, int level) {
    (void)kind;
    const int owned = std::min(std::max(0, level), kMaxUnitLevel);
    return 1.0f + static_cast<float>(owned) * kTrainHealthPerLevel;
}

float trainedDamage(int kind, int level) {
    (void)kind;
    const int owned = std::min(std::max(0, level), kMaxUnitLevel);
    return 1.0f + static_cast<float>(owned) * kTrainDamagePerLevel;
}

bool heroButtonHit(float screenX, float screenY) {
    if (screenY < kButtonY || screenY > kButtonY + kButtonHeight) return false;
    return screenX >= kHeroButtonX &&
           screenX <= kHeroButtonX + kHeroButtonWidth;
}

int buttonAt(float screenX, float screenY) {
    if (screenY < kButtonY || screenY > kButtonY + kButtonHeight) return -1;

    for (int slot = 0; slot < visibleButtonCount(); ++slot) {
        const float left = buttonLeft(slot);
        if (screenX >= left && screenX <= left + kButtonWidth) {
            return kindForButton(slot);
        }
    }
    return -1;
}

Camera* findCamera(World& world) {
    for (auto& entry : world.view<Camera>()) return &entry.second;
    return nullptr;
}

float frontLineX(World& world, bool leftSide) {
    const float direction = facing(leftSide);

    bool found = false;
    float front = 0.0f;
    // The unit pool. This is the most-called query in the game — the camera,
    // the minimap's two markers and the enemy cannon all ask it every frame —
    // and it was walking the scenery to answer.
    //
    // The answer is a coordinate rather than an entity, so a tie between two
    // units standing on the same x returns the same number whichever the pool
    // yields first. That is the reason this one is safe to move and `fight`
    // is not: see the note there.
    for (auto& [entity, unitRef] : world.view<Unit>()) {
        (void)unitRef;
        Team* team = world.getComponent<Team>(entity);
        Transform* transform = world.getComponent<Transform>(entity);
        if (!team || team->leftSide != leftSide || !transform) continue;

        // "Frontmost" is furthest along your own direction of travel, which
        // is the largest x for the left side and the smallest for the right.
        // Multiplying by the direction lets one comparison serve both.
        if (!found || transform->x * direction > front * direction) {
            front = transform->x;
            found = true;
        }
    }
    if (found) return front;

    // Nothing on the field: your advance has reached your own front door.
    const Entity castle = findCastle(world, leftSide);
    if (castle != kInvalidEntity) {
        if (Transform* transform = world.getComponent<Transform>(castle)) {
            return transform->x + kCastleWidth / 2.0f;
        }
    }
    // Unreachable while a battle is running — a side always has a castle, and
    // the scene stops calling this the moment one falls. Kept because the
    // function has to return something when findCastle finds nothing, and
    // noted because no test can reach it: breaking this line on purpose left
    // every test passing, which is a fact about the line, not the tests.
    return leftSide ? kLeftCastleX : kRightCastleX;
}

Entity spawnUnit(World& world, bool leftSide, int kind) {
    const UnitKind& stats = kindOf(kind);
    const float x = leftSide ? kLeftSpawnX : kRightSpawnX;

    Entity unit = world.createEntity();
    // A flyer sits in the sky instead of standing on the ground line. That
    // one difference is the whole of "a second lane" as far as position goes;
    // everything else about altitude is the two flags on the roster row.
    const float y = stats.flying ? kFlyingY : kGroundY - stats.height;
    world.addComponent(unit, Transform{x, y, 0.0f});
    world.addComponent(unit, Velocity{stats.speed * facing(leftSide), 0.0f});

    Sprite sprite;
    sprite.width = static_cast<int>(stats.width);
    sprite.height = static_cast<int>(stats.height);
    if (leftSide) {
        sprite.r = stats.leftR; sprite.g = stats.leftG; sprite.b = stats.leftB;
    } else {
        sprite.r = stats.rightR; sprite.g = stats.rightG; sprite.b = stats.rightB;
    }
    sprite.layer = kFieldLayer;

    // Artwork, if this row has any and anything has handed us a texture cache.
    //
    // Both conditions matter. No cache is every test and both simulators, which
    // run with no window at all — and a unit that silently falls back to a block
    // there is exactly right, because what those measure is the fight, not the
    // picture.
    //
    // There are two paths, and the first is the real one now. A sheet that
    // art.txt describes becomes a separate ART FIGURE that follows the unit
    // (see Art.h): drawn at a readable size, anchored by its feet, choosing a
    // row of the sheet by what the unit is doing. The unit's own Sprite stays
    // as its footprint and goes invisible. The second path is slice 5b's
    // original — texture the unit's own sprite and loop one row — kept for a
    // sheet named with frame_width/frame_height but not measured into art.txt.
    const bool measured = textureCache && stats.sheet && stats.sheet[0] != '\0' &&
                          sheetInfo(stats.sheet) != nullptr;
    bool animated = false;
    if (!measured && textureCache && stats.sheet && stats.sheet[0] != '\0' &&
        stats.frameWidth > 0 && stats.frameHeight > 0) {
        if (SDL_Texture* texture = textureCache->load(stats.sheet)) {
            sprite.texture = texture;
            sprite.srcX = 0;
            sprite.srcY = 0;
            sprite.srcW = stats.frameWidth;
            sprite.srcH = stats.frameHeight;

            // The tint is dropped for real artwork. r/g/b multiply into a
            // texture, so leaving a unit's team colour in place would wash
            // every drawn frame with it; sides are told apart by which way
            // they face instead.
            sprite.r = sprite.g = sprite.b = 255;

            // Drawn walking one way and mirrored for the other, which is the
            // whole reason Sprite.flipX exists: an artist draws one direction.
            sprite.flipX = !leftSide;
            animated = true;
        }
    }

    world.addComponent(unit, sprite);

    if (animated) {
        Animation animation;
        animation.frameCount = std::max(1, stats.frameCount);
        animation.frameWidth = stats.frameWidth;
        animation.secondsPerFrame =
            stats.frameSeconds > 0.0f ? stats.frameSeconds : 0.12f;
        animation.loop = true;
        world.addComponent(unit, animation);
    }

    world.addComponent(unit, Team{leftSide});

    Unit component;
    component.kind = kind;
    // Training is YOURS. The opponent fields the roster as written, or every
    // level bought would arm both sides and buy nothing — the same reason
    // WEAPONS only scales the player's blows.
    const float trained =
        leftSide ? trainedHealth(kind, trainingLevel(kind)) : 1.0f;
    component.health = stats.health * trained;
    component.maxHealth = stats.health * trained;
    component.hitsAir = stats.hitsAir;  // the hero's path may override this
    world.addComponent(unit, component);
    // The stick figure is what stands in FOR artwork, so a unit that has
    // artwork does not get one. Drawing both would put a line drawing over the
    // top of the frame it was invented to replace.
    //
    // `figure` stays kInvalidEntity in that case, which every path already
    // copes with: a death checks it before destroying it, and animateUnits
    // walks the figures that exist rather than assuming one per unit.
    if (measured) {
        const Entity art = createArtFigure(world, unit, kind, leftSide);
        if (art != kInvalidEntity) {
            world.getComponent<Unit>(unit)->figure = art;
            // The footprint stays — targeting, queueing and the camera all read
            // it — but it is no longer what anyone should see.
            world.getComponent<Sprite>(unit)->a = 0;
            animated = true;
        }
    }
    if (!animated) {
        world.getComponent<Unit>(unit)->figure = createFigure(world, unit, sprite);
    }

    // Counted here, the one door every unit comes through — the bar, the
    // hero's button and the opponent's waves all end in this function — so the
    // record of what each side sent cannot disagree with what took the field.
    if (kind >= 0 && kind < kMaxUnitKinds) {
        if (Session* session = findSession(world)) {
            ++(leftSide ? session->record.sent : session->record.enemySent)[kind];
        }
    }

    // A different pitch per kind, so you can hear what you just sent without
    // looking away from the front line.
    if (audioDevice) {
        audioDevice->play(Waveform::Square, 240.0f + 60.0f * static_cast<float>(kind),
                          0.05f, 0.12f);
    }
    return unit;
}

void setAudioDevice(AudioDevice* audio) { audioDevice = audio; }

void setTextureCache(TextureCache* textures) { textureCache = textures; }
TextureCache* currentTextureCache() { return textureCache; }

// --- The clock, and explaining a defeat -------------------------------------

std::string clockText(float seconds) {
    const int whole = std::max(0, static_cast<int>(std::ceil(seconds)));
    const int tens = (whole % 60) / 10;
    const int units = whole % 10;
    return std::to_string(whole / 60) + ":" + static_cast<char>('0' + tens) +
           static_cast<char>('0' + units);
}

float swarmGap(float secondsIntoSwarm) {
    const float into = std::max(0.0f, secondsIntoSwarm);
    const float rate = std::min(kSwarmMaxRate, kSwarmStartRate + kSwarmRateGrowth * into);
    return 1.0f / rate;
}

float swarmStrength(float secondsIntoSwarm) {
    return 1.0f + std::max(0.0f, secondsIntoSwarm) / kSwarmStrengthenSeconds;
}

std::string pluralName(const char* name, int count) {
    std::string word = name ? name : "";
    if (count == 1 || word.empty()) return word;

    auto endsWith = [&word](const char* tail) {
        const std::size_t length = std::char_traits<char>::length(tail);
        return word.size() >= length &&
               word.compare(word.size() - length, length, tail) == 0;
    };
    if (endsWith("MAN")) {  // PIKEMAN, and any data file's LANCEMAN
        word[word.size() - 2] = 'E';
        return word;
    }
    if (endsWith("S") || endsWith("X") || endsWith("CH") || endsWith("SH")) {
        return word + "ES";
    }
    return word + "S";
}

namespace {

// The thresholds a defeat is judged against. Each is the point past which the
// fact stops being noise and starts being the reason — and each is a number a
// person can check against the battle they just lost.
//
// A sky threat is two flyers, not one: the stage table found that a single
// griffin is answered by whatever happens to be walking beneath it, and it
// takes a wing before anti-air is worth a slot.
constexpr int kFlyersThatNeedAnAnswer = 2;
// Flyers that did this share of everything you took were the problem even if
// something you carried could reach them — you did not carry enough of it.
constexpr float kFlyerShareThatMatters = 0.40f;
// A hero that falls this soon after coming out, with this few in front of it,
// was thrown away rather than spent.
constexpr float kHeroWastedWithin = 20.0f;
constexpr int kHeroNeedsAnEscortOf = 2;
// Not worth mentioning the hero to a player who lost before it could matter.
constexpr float kHeroMattersAfter = 30.0f;
// Reach past this is range, and range needs somebody standing in front of it.
constexpr float kRangedReach = 90.0f;
// Gold still in the purse at the end, that could have been most of a soldier.
constexpr float kGoldWorthMentioning = 100.0f;

// "PIKEMEN OR ARCHERS": the cheapest two things that reach the sky, from the
// roster rather than from a list written here, so a data file's new anti-air
// unit is recommended without anybody editing this.
std::string skyAnswers() {
    std::vector<int> answers;
    const int hero = heroKindIndex();
    for (int kind = 0; kind < unitKindCount(); ++kind) {
        if (kind != hero && unitKind(kind).hitsAir) answers.push_back(kind);
    }
    std::stable_sort(answers.begin(), answers.end(), [](int a, int b) {
        return unitKind(a).cost < unitKind(b).cost;
    });
    if (answers.empty()) return "";
    std::string text = pluralName(unitKind(answers[0]).name, 2);
    if (answers.size() > 1) text += " OR " + pluralName(unitKind(answers[1]).name, 2);
    return text;
}

// "9 SOLDIERS, 4 GRIFFINS AND 3 RUNNERS", most first. Past four kinds the tail
// is summarised, because the line has a window to fit in.
std::string armyText(const int* counts) {
    std::vector<int> kinds;
    const int roster = std::min(unitKindCount(), kMaxUnitKinds);
    for (int kind = 0; kind < roster; ++kind) {
        if (counts[kind] > 0) kinds.push_back(kind);
    }
    std::stable_sort(kinds.begin(), kinds.end(),
                     [counts](int a, int b) { return counts[a] > counts[b]; });

    std::vector<std::string> parts;
    int others = 0;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const int count = counts[kinds[i]];
        if (i < 3 || kinds.size() == 4) {
            parts.push_back(std::to_string(count) + " " +
                            pluralName(unitKind(kinds[i]).name, count));
        } else {
            others += count;
        }
    }
    if (others > 0) parts.push_back(std::to_string(others) + " MORE");

    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) text += (i + 1 == parts.size()) ? " AND " : ", ";
        text += parts[i];
    }
    return text;
}

std::string percent(float share) {
    return std::to_string(static_cast<int>(std::lround(share * 100.0f))) + "%";
}

}  // namespace

DefeatReport explainDefeat(const Session& session) {
    // A battle lost to the swarm is explained by the ten minutes before it.
    // Afterwards the swarm is always what hurt most, and "their soldiers did
    // 90% of the damage" would be true of every such defeat and useful in none.
    const BattleRecord& record = session.swarming ? session.beforeSwarm : session.record;
    const int roster = std::min(unitKindCount(), kMaxUnitKinds);
    const int hero = heroKindIndex();
    DefeatReport report;

    // What happened.
    const std::string when = clockText(std::floor(session.elapsed));
    report.headline =
        session.swarming
            ? "THE SWARM OF " + std::to_string(session.record.swarmSent) +
                  " BROKE YOUR CASTLE AT " + when
            : "YOUR CASTLE FELL AT " + when;

    const std::string theirs = armyText(record.enemySent);
    if (!theirs.empty()) report.enemyArmy = "THEY SENT " + theirs;

    // The facts every reason below is judged on.
    int enemyFlyers = 0;
    int flyerKind = -1;
    float flyerDamage = 0.0f;
    float taken = record.cannonDamageTaken;
    int worstKind = -1;
    for (int kind = 0; kind < roster; ++kind) {
        taken += record.damageTakenFrom[kind];
        if (worstKind < 0 || record.damageTakenFrom[kind] > record.damageTakenFrom[worstKind]) {
            worstKind = kind;
        }
        if (!unitKind(kind).flying) continue;
        enemyFlyers += record.enemySent[kind];
        flyerDamage += record.damageTakenFrom[kind];
        if (record.enemySent[kind] > 0 &&
            (flyerKind < 0 || record.enemySent[kind] > record.enemySent[flyerKind])) {
            flyerKind = kind;
        }
    }

    bool carriedReachesSky = record.heroReachesSky;
    for (int kind : record.carried) {
        if (kind >= 0 && kind < roster && unitKind(kind).hitsAir) carriedReachesSky = true;
    }

    int kindsSent = 0;
    int onlyKind = -1;
    int unitsSent = 0;
    int ranged = 0;
    int rangedKind = -1;
    int front = 0;
    for (int kind = 0; kind < roster; ++kind) {
        if (kind == hero || record.sent[kind] == 0) continue;
        ++kindsSent;
        onlyKind = kind;
        unitsSent += record.sent[kind];
        if (unitKind(kind).flying) continue;
        if (unitKind(kind).range >= kRangedReach) {
            ranged += record.sent[kind];
            if (rangedKind < 0 || record.sent[kind] > record.sent[rangedKind]) {
                rangedKind = kind;
            }
        } else {
            front += record.sent[kind];
        }
    }

    // Why, most useful first. Each is only said when the record shows it, and
    // each is the fact followed by the fix.
    auto add = [&report](const std::string& line) {
        if (static_cast<int>(report.reasons.size()) < kMaxDefeatReasons) {
            report.reasons.push_back(line);
        }
    };

    const bool skyUnanswered =
        enemyFlyers >= kFlyersThatNeedAnAnswer && !carriedReachesSky && flyerKind >= 0;
    if (skyUnanswered) {
        std::string line = "NOTHING YOU CARRIED HITS THEIR " +
                           pluralName(unitKind(flyerKind).name, 2);
        const std::string answers = skyAnswers();
        if (!answers.empty()) line += " - TRY " + answers;
        add(line);
    }

    if (kindsSent == 1 && unitsSent >= 3) {
        add("YOU SENT ONLY " + pluralName(unitKind(onlyKind).name, 2) +
            " - ONE KIND OF UNIT IS NOT AN ARMY");
    }

    if (record.heroSummonedAt >= 0.0f && record.heroFellAt >= 0.0f &&
        record.heroFellAt - record.heroSummonedAt <= kHeroWastedWithin &&
        record.heroEscort < kHeroNeedsAnEscortOf) {
        const int lasted = static_cast<int>(
            std::lround(record.heroFellAt - record.heroSummonedAt));
        add("YOUR HERO WENT OUT ALONE AND FELL IN " + std::to_string(lasted) +
            "S - SEND IT BEHIND A LINE");
    }

    if (ranged >= 3 && front < ranged && rangedKind >= 0) {
        add("MORE " + pluralName(unitKind(rangedKind).name, 2) +
            " THAN FRONT LINE - THEY NEED SOMEONE TO HIDE BEHIND");
    }

    const bool flyersWereTheProblem =
        !skyUnanswered && flyerKind >= 0 && taken > 0.0f &&
        flyerDamage >= kFlyerShareThatMatters * taken;
    if (flyersWereTheProblem) {
        add("THEIR " + pluralName(unitKind(flyerKind).name, 2) + " DID " +
            percent(flyerDamage / taken) + " OF THE DAMAGE - CARRY MORE THAT HITS AIR");
    }

    // Ten minutes that never broke through: how little reached their castle.
    if (session.swarming) {
        const int dealt = static_cast<int>(record.castleDamageDealt);
        const int walls = session.enemyUpgrades[static_cast<int>(Upgrade::Walls)];
        if (dealt <= 0) {
            add("NOTHING YOU SENT REACHED THEIR CASTLE IN TEN MINUTES");
        } else if (walls >= 2) {
            add("YOU HIT THEIR CASTLE FOR " + std::to_string(dealt) +
                " WHILE THEY REBUILT IT " + std::to_string(walls) + " TIMES");
        } else {
            add("YOU HIT THEIR CASTLE FOR ONLY " + std::to_string(dealt) +
                " BEFORE THE SWARM CAME");
        }
    }

    if (hero >= 0 && record.heroSummonedAt < 0.0f && session.elapsed >= kHeroMattersAfter) {
        add("YOU NEVER SUMMONED YOUR HERO - PRESS H ONCE THE LINES MEET");
    }

    if (session.gold >= kGoldWorthMentioning) {
        add("YOU ENDED WITH " + std::to_string(static_cast<int>(session.gold)) +
            " GOLD UNSPENT - GOLD IN THE BANK HOLDS NO LINE");
    }

    // And whatever hurt most, if nothing above has already named it.
    const bool worstIsCannon =
        record.cannonDamageTaken > (worstKind >= 0 ? record.damageTakenFrom[worstKind] : 0.0f);
    const float worst = worstIsCannon ? record.cannonDamageTaken
                                      : (worstKind >= 0 ? record.damageTakenFrom[worstKind] : 0.0f);
    const bool alreadyNamed =
        !worstIsCannon && worstKind >= 0 && unitKind(worstKind).flying &&
        (skyUnanswered || flyersWereTheProblem);
    if (taken > 0.0f && worst > 0.0f && !alreadyNamed) {
        const std::string who =
            worstIsCannon ? "CANNON" : pluralName(unitKind(worstKind).name, 2);
        add("THEIR " + who + " DID THE MOST DAMAGE - " + percent(worst / taken) +
            " OF WHAT YOU TOOK");
    }

    return report;
}

namespace {

// --- Scenes ----------------------------------------------------------------

// Splits a line at spaces so that no piece is wider than `maxWidth` at this
// scale. The reasons on the defeat screen are built from roster names, and a
// data file is free to name a unit something long.
std::vector<std::string> wrapText(const std::string& text, int scale, int maxWidth) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        const std::string joined = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(joined, scale) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = joined;
        }
        start = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

class GameOverScene : public Scene {
public:
    GameOverScene(bool playerWon, DefeatReport report)
        : playerWon_(playerWon), report_(std::move(report)) {}

    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        if (!playerWon_) {
            buildDefeat(world);
            return;
        }
        owned_.push_back(createCenteredText(world, "VICTORY", 170, 6, 140, 230,
                                            150, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "R FOR THE CAMPAIGN", 280, 2,
                                            170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 310, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World& world, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_R)) {
            scenes.pop();
            return;
        }
        // Back to the stage list, where A changes the army and H the hero.
        //
        // A loss used to offer exactly two things: the same battle again, with
        // the same army, or closing the game. So a defeat screen that says
        // "nothing you carried can hit their griffins" would have been advice
        // the player had no way to take.
        if (!playerWon_ && input.wasKeyPressed(SDL_SCANCODE_Q)) {
            if (Session* session = findSession(world)) session->leaveAfterDefeat = true;
            scenes.pop();
        }
    }

    bool simulatesWorld() const override { return false; }

private:
    // DEFEAT, then what happened, what they sent, and why — on a panel of its
    // own, because the HUD underneath shows through the backdrop and a line of
    // advice laid over the upgrade buttons is a line nobody reads.
    void buildDefeat(World& world) {
        constexpr float kPanelWidth =
            static_cast<float>(kWindowWidth) - 2.0f * kDefeatPanelX;
        constexpr int kLine = 26;

        const Entity panel = createRect(world, kDefeatPanelX, 64.0f, kPanelWidth, 0.0f,
                                        14, 14, 26, kOverlayLayer);
        Sprite* plate = world.getComponent<Sprite>(panel);
        plate->a = 225;
        plate->screenSpace = true;
        owned_.push_back(panel);

        int y = 84;
        owned_.push_back(createCenteredText(world, "DEFEAT", y, 6, 240, 110, 110,
                                            kOverlayTextLayer));
        y += 60;

        auto lines = [&](const std::string& text, unsigned char r, unsigned char g,
                         unsigned char b) {
            for (const std::string& line : wrapText(text, 2, kDefeatTextWidth)) {
                owned_.push_back(createCenteredText(world, line, y, 2, r, g, b,
                                                    kOverlayTextLayer));
                y += kLine;
            }
        };

        lines(report_.headline, 235, 205, 165);
        if (!report_.enemyArmy.empty()) lines(report_.enemyArmy, 165, 165, 190);
        y += 14;
        for (const std::string& reason : report_.reasons) lines(reason, 240, 220, 140);

        y += 22;
        lines("R TO TRY AGAIN", 170, 170, 195);
        lines("Q FOR THE STAGE LIST - CHANGE YOUR ARMY OR HERO", 170, 170, 195);
        lines("ESC TO QUIT", 140, 140, 165);

        plate->height = y - 64 + 10;
    }

    bool playerWon_;
    DefeatReport report_;
    std::vector<Entity> owned_;
};

class PauseScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "PAUSED", 200, 6,
                                            235, 235, 235, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "P TO RESUME", 280, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_P)) scenes.pop();
    }

    bool simulatesWorld() const override { return false; }

private:
    std::vector<Entity> owned_;
};

class PlayScene : public Scene {
public:
    void onEnter(World& world) override {
        sessionEntity_ = world.createEntity();
        world.addComponent(sessionEntity_, Session{});

        // Which stage this battle is. Read once, here, so everything below
        // works from one snapshot rather than consulting the campaign
        // mid-fight.
        const Campaign& campaign = campaignOf(world);
        stage_ = campaign.currentStage;

        for (int perk = 0; perk < kPerkCount; ++perk) perks_[perk] = campaign.perks[perk];
        damageScale_ =
            1.0f + perks_[static_cast<int>(Perk::Damage)] *
                       perkKind(static_cast<int>(Perk::Damage)).effect;

        // The hero's path, resolved once here rather than looked up mid-fight,
        // for the same reason the stage is: a battle should be decided by one
        // snapshot taken at the start.
        readHeroPath(campaign);

        // What you walked in carrying, and what it has been trained to. Also a
        // snapshot: the army screen is closed while a battle runs, so nothing
        // can change underneath it.
        setLoadout(campaign.loadout, kLoadoutSlots);
        setTrainingLevels(campaign.unitLevels, kMaxUnitKinds);

        Session& fresh = *world.getComponent<Session>(sessionEntity_);
        applyStage(fresh, stage_);
        beginRecord(fresh);

        // TREASURY is a head start rather than a rate, which is what makes it
        // worth buying early and worth less later.
        fresh.gold += perks_[static_cast<int>(Perk::Purse)] *
                      perkKind(static_cast<int>(Perk::Purse)).effect;

        // GRANARY is the rate. Between them they are the two halves of an
        // economy that could not exist inside a battle: one pays now, one pays
        // over the whole fight, and neither costs you a soldier at the moment
        // the line is being decided.
        fresh.bonusGoldPerSecond =
            perks_[static_cast<int>(Perk::Granary)] *
            perkKind(static_cast<int>(Perk::Granary)).effect;

        // One camera, on its own entity. The renderer picks up the first one
        // it finds; before this game the only thing that ever moved it was
        // Asteroids' screen shake, a few pixels at a time.
        cameraEntity_ = world.createEntity();
        world.addComponent(cameraEntity_, Camera{});

        buildField(world);

        weather_.start(world, textureCache);
        weatherText_ = createText(world, "F6 WEATHER - F11 FLASH", 16, 164, 1,
                                  140, 160, 180, kHudLayer);
        hud_.push_back(weatherText_);
        ambientText_ = createText(world, "", 16, 180, 1, 140, 160, 180, kHudLayer);
        hud_.push_back(ambientText_);

        goldText_ = createText(world, "GOLD 150", 16, 14, 3, 235, 220, 150,
                               kHudLayer);
        leftHealthText_ = createText(world, "", 16, 48, 2, 140, 200, 240,
                                     kHudLayer);
        // Sat just above the upgrade panel rather than at a fixed 48, because
        // raising that panel to clear the castles would otherwise have run it
        // straight through this label — trading one overlap for another, which
        // is how the hero button ended up on top of the spawn bar.
        rightHealthText_ = createText(world, "",
                                      kWindowWidth - 220,
                                      static_cast<int>(kUpgradeY) - 22, 2,
                                      240, 150, 130, kHudLayer);
        hud_.push_back(goldText_);
        hud_.push_back(leftHealthText_);
        hud_.push_back(rightHealthText_);
        popText_ = createText(world, "", 16, 78, 2, 170, 170, 195, kHudLayer);
        hud_.push_back(popText_);

        buildSpawnBar(world);
        buildHeroButton(world);
        buildUpgradePanel(world);
        buildSpellPanel(world);

        cannonText_ = createText(world, "", 16, 112, 2, 220, 200, 140, kHudLayer);
        hud_.push_back(cannonText_);

        hud_.push_back(createText(world, "DRAG OR ARROWS TO LOOK - P PAUSES",
                                  static_cast<int>(kButtonX),
                                  static_cast<int>(kButtonY) - 22, 2,
                                  130, 130, 155, kHudLayer));

        hud_.push_back(createText(world, stageKind(stage_).name, 16, 140, 2,
                                  200, 190, 150, kHudLayer));

        buildMinimap(world);

        // Under the minimap, which is where the eye already goes to ask how
        // the battle is going. Filled in by refreshHud.
        clockText_ = createText(world, "", 0,
                                static_cast<int>(kMinimapY + kMinimapHeight) + 8, 2,
                                140, 150, 170, kHudLayer);
        hud_.push_back(clockText_);

        // Empty until the swarm arrives. A HUD text rather than something with
        // a Lifetime, because Lifetime only runs while a scene simulates the
        // world — a banner that outlived its battle would sit on the stage list.
        swarmBanner_ = createText(world, "", 0, 214, 4, 245, 110, 90, kHudLayer);
        hud_.push_back(swarmBanner_);

        // Start looking at your own castle rather than at the origin, so the
        // first frame is not a lurch.
        snapCameraToTarget(world);
    }

    void onExit(World& world) override {
        clearField(world);
        weather_.clear(world);
        environment_.clear(world);
        world.destroyLater(sessionEntity_);
        world.destroyLater(cameraEntity_);
        for (Entity entity : hud_) world.destroyLater(entity);
        hud_.clear();
    }

    void onResume(World& world) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session || !session->gameOver) return;  // returning from pause

        // A win ends this battle for good: the next stage is open, so the
        // player goes back to the list rather than replaying what they just
        // beat. A loss restarts the same stage where they stand — unless the
        // player asked to go back, which is how a loss the defeat screen
        // blamed on the army gets a different army.
        //
        // The pop happens in update() rather than here, because onResume has
        // no SceneStack to ask — and popping the scene that is mid-resume
        // would be a poor idea even if it did.
        if (session->playerWon || session->leaveAfterDefeat) {
            returnToCampaign_ = true;
            return;
        }

        clearField(world);
        *session = Session{};

        // Re-applied after the reset, which wipes it. Forgetting this makes
        // every retry of stage eight secretly a retry of stage one.
        applyStage(*session, stage_);
        beginRecord(*session);

        buildField(world);
        snapCameraToTarget(world);
        overlayShown_ = false;
        shownGold_ = -1;
        shownPopulation_ = -1;
        shownClock_ = -1;
    }

    // What the player walked in with, written into the record before a blow
    // is struck. The loadout and the path are both snapshots taken when the
    // battle began, so this is what the battle was fought with.
    void beginRecord(Session& session) const {
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            session.record.carried[slot] = loadoutKind(slot);
        }
        session.record.heroReachesSky = heroHitsAir_;
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        if (returnToCampaign_) {
            scenes.pop();
            return;
        }

        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session) return;

        if (session->gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(
                    session->playerWon,
                    session->playerWon ? DefeatReport{} : explainDefeat(*session)));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        // The clock runs only here — past the pause and the end of the battle,
        // both of which return above — so a paused battle loses no time.
        session->elapsed += dt;
        if (!session->swarming && session->elapsed >= kBattleSeconds) beginSwarm(world, *session);

        session->cannonCooldown = std::max(0.0f, session->cannonCooldown - dt);

        if(input.wasKeyPressed(SDL_SCANCODE_F2)) {
            const auto next=Environment((static_cast<int>(environment_.current())+1)%static_cast<int>(Environment::Count));
            if(environment_.select(world,textureCache,next) && !compatibleWeather(next,weather_.settings.preset))
                weather_.settings.preset=WeatherPreset::Clear;
        }
        if(input.wasKeyPressed(SDL_SCANCODE_F3)) {
            environment_.strong=!environment_.strong;
            weather_.settings.preset=environment_.strong?strongWeather(environment_.current()):WeatherPreset::Clear;
        }
        const auto oldWeather=weather_.settings.preset;
        weather_.controls(input);
        if(input.wasKeyPressed(SDL_SCANCODE_F6)) weather_.settings.preset=nextCompatibleWeather(environment_.current(),oldWeather);
        environment_.controls(input);
        environment_.update(world,dt);
        if(auto* text=world.getComponent<Text>(ambientText_)) text->value=environment_.description();
        weather_.update(world, dt);
        if (auto* text = world.getComponent<Text>(weatherText_))
            text->value = std::string("F2 ") + environmentName(environment_.current()) + " F3 " + (environment_.strong?"STRONG":"CALM") + " F6 " + weatherName(weather_.settings.preset) + " F11 " +
                (weather_.settings.lightning == LightningMode::Off ? "OFF" :
                 weather_.settings.lightning == LightningMode::Reduced ? "REDUCED" : "NORMAL");

        earnGold(*session, dt);
        handleUpgrades(world, *session, input);
        handleSpawning(world, *session, input, dt);
        updateSwarm(world, *session, dt);
        updateEnemyCannon(world, *session, dt);
        fight(world, *session, dt);
        removeTheDead(world, *session);

        // After the dead are gone, so the camera never chases a corpse for a
        // frame, and after the fight, so the minimap shows this frame's front
        // line rather than last frame's.
        healAroundTheHero(world, dt);
        updateSpells(world, *session, input, dt);
        updateCannonballs(world, dt);

        // Last, because a castle can be finished by a blow, a bolt or a shell,
        // and asking before all three have landed is how the answer used to
        // depend on which one got there first. See settleBattle.
        settleBattle(world, *session);

        animateUnits(world, dt);
        updateCorpses(world, dt);
        updateCamera(world, *session, input, dt);
        refreshHud(world, *session);
        refreshMinimap(world);
    }

private:
    WeatherSystem weather_;
    EnvironmentSystem environment_;
    Entity ambientText_ = 0;
    Entity weatherText_ = 0;
    void buildField(World& world) {
        // Scenery first, so it holds the lowest entity ids as well as the
        // lowest layers. Layers alone would be enough, but keeping creation
        // order and draw order agreeing means the id tiebreak never has to
        // arbitrate between two things at the same depth.
        buildScenery(world);

        // The ground, drawn as one wide bar so the units have something to
        // stand on rather than floating in the dark. It spans the WORLD now,
        // not the window, or it would run out halfway through the first push.
        field_.push_back(createRect(world, 0.0f, kGroundY, kWorldWidth,
                                    static_cast<float>(kWindowHeight) - kGroundY,
                                    38, 42, 52, kFieldLayer));

        buildDistanceMarkers(world);

        Entity left = createRect(world, kLeftCastleX, kGroundY - kCastleHeight,
                                 kCastleWidth, kCastleHeight, 70, 120, 165,
                                 kFieldLayer);
        world.addComponent(left, Team{true});
        // RAMPARTS is permanent, so it is applied when the castle is built
        // rather than bought mid-battle like WALLS.
        world.addComponent(left, Castle{kCastleHealth + perks_[static_cast<int>(Perk::Fortify)] *
                                            perkKind(static_cast<int>(Perk::Fortify)).effect});
        field_.push_back(left);

        Entity right = createRect(world, kRightCastleX, kGroundY - kCastleHeight,
                                  kCastleWidth, kCastleHeight, 170, 90, 80,
                                  kFieldLayer);
        world.addComponent(right, Team{false});
        // The stage decides how much castle there is to chew through, which is
        // half of what makes a later stage longer as well as harder.
        world.addComponent(right, Castle{stageKind(stage_).enemyCastleHealth});
        field_.push_back(right);

        dressCastle(world, kLeftCastleX, 24, 44, 66);
        dressCastle(world, kRightCastleX, 66, 34, 32);
    }

    // Battlements and a gate, drawn over a castle's block.
    //
    // The castle is what the whole game is about — it is the win condition at
    // one end and the lose condition at the other — and it was a plain
    // rectangle. Nothing marked it as a building, which side it belonged to
    // beyond its colour, or which end of it the fighting reaches.
    //
    // Built out of `Polygon`, exactly like the stick figures over the units,
    // for exactly the same reason: this project has no art and no artist, and
    // a few line segments cost nothing and need no engine work. Drawn a shade
    // darker than the block so the detail reads against it.
    void dressCastle(World& world, float x, unsigned char r, unsigned char g,
                     unsigned char b) {
        const float top = kGroundY - kCastleHeight;
        const float w = kCastleWidth;

        Entity trim = world.createEntity();
        world.addComponent(trim, Transform{x, top, 0.0f});

        Polygon lines;
        lines.closed = false;
        // Four crenellations along the top, as one unbroken stroke.
        const float notch = w / 7.0f;
        lines.points = {
            Vec2{0.0f, 12.0f},        Vec2{0.0f, 0.0f},
            Vec2{notch, 0.0f},        Vec2{notch, 10.0f},
            Vec2{notch * 2, 10.0f},   Vec2{notch * 2, 0.0f},
            Vec2{notch * 3, 0.0f},    Vec2{notch * 3, 10.0f},
            Vec2{notch * 4, 10.0f},   Vec2{notch * 4, 0.0f},
            Vec2{notch * 5, 0.0f},    Vec2{notch * 5, 10.0f},
            Vec2{notch * 6, 10.0f},   Vec2{notch * 6, 0.0f},
            Vec2{w, 0.0f},            Vec2{w, 12.0f},
        };
        lines.r = r;
        lines.g = g;
        lines.b = b;
        // Above the block it decorates. Within a layer the renderer sorts by
        // entity id and ids rise with creation order, so being built after the
        // castle is what puts it on top — the same quiet guarantee the stick
        // figures rely on.
        lines.layer = kFieldLayer;
        world.addComponent(trim, lines);
        field_.push_back(trim);

        // The gate, a separate stroke because it does not join the battlements.
        Entity gate = world.createEntity();
        world.addComponent(gate,
                           Transform{x + w / 2.0f, kGroundY, 0.0f});

        Polygon arch;
        arch.closed = false;
        arch.points = {Vec2{-14.0f, 0.0f}, Vec2{-14.0f, -26.0f},
                       Vec2{-8.0f, -34.0f}, Vec2{8.0f, -34.0f},
                       Vec2{14.0f, -26.0f}, Vec2{14.0f, 0.0f}};
        arch.r = r;
        arch.g = g;
        arch.b = b;
        arch.layer = kFieldLayer;
        world.addComponent(gate, arch);
        field_.push_back(gate);
    }

    // --- Scenery -----------------------------------------------------------
    //
    // Everything here goes into field_, so a restart clears it along with the
    // army. Nothing is random: positions come from a cheap integer hash of the
    // index, so every battle looks the same and a test can rely on it.

    static float jitter(int seed, float low, float high) {
        // A small deterministic hash. Not a good one — it only has to spread
        // a dozen hills out without a pattern the eye can catch, and being
        // reproducible matters far more than being uniform.
        const unsigned int mixed = (static_cast<unsigned int>(seed) * 2654435761u) >> 16;
        const float unit = static_cast<float>(mixed % 1000u) / 1000.0f;
        return low + unit * (high - low);
    }

    void buildScenery(World& world) {
        if (environment_.select(world, textureCache, environment_.current())) return;
        if (buildVectorScenery(world, textureCache, field_)) {
            buildForeground(world);
            return;
        }
        // Sky: screen-space, because it genuinely does not move. Three bands
        // standing in for a gradient the renderer cannot draw.
        field_.push_back(makeSky(world, 0.0f, 150.0f, 26, 30, 46));
        field_.push_back(makeSky(world, 150.0f, 120.0f, 34, 38, 54));
        field_.push_back(makeSky(world, 270.0f, kGroundY - 270.0f, 44, 46, 60));

        // Brighter than they were, because `Polygon` STROKES rather than
        // fills — there is no filled-polygon call in this engine — so a hill
        // is a triangle outline, not a solid mass. At the old values the three
        // bands were a few pixels of near-black on near-black and the whole
        // parallax effect was invisible in a screenshot. Lifted until the
        // ridgelines read, which is the same line-art the units are drawn in
        // rather than a different style bolted on.
        buildHills(world, kFarParallax, kFarLayer, 150.0f, 320.0f, 66, 72, 98, 11);
        buildHills(world, kMidParallax, kMidLayer, 100.0f, 240.0f, 56, 66, 86, 23);
        buildHills(world, kNearParallax, kNearLayer, 60.0f, 180.0f, 48, 58, 74, 37);
        buildForeground(world);
    }

    Entity makeSky(World& world, float y, float height, unsigned char r,
                   unsigned char g, unsigned char b) {
        const Entity entity =
            createRect(world, 0.0f, y, static_cast<float>(kWindowWidth), height,
                       r, g, b, kSkyLayer);
        world.getComponent<Sprite>(entity)->screenSpace = true;
        return entity;
    }

    // A row of triangles along the horizon. Closed polygons rather than
    // sprites, because a hill is not a rectangle and the renderer already
    // draws outlines.
    void buildHills(World& world, float parallax, int layer, float minHeight,
                    float spacing, unsigned char r, unsigned char g,
                    unsigned char b, int seed) {
        // One step past the far edge as well as one before the near one: a
        // hill is drawn either side of its origin, so the band has to be
        // seeded slightly beyond where it needs to be visible.
        const float width = bandWidth(parallax) + spacing;
        for (float x = -spacing; x < width; x += spacing) {
            const int index = static_cast<int>(x / spacing) + seed;
            const float height = jitter(index, minHeight, minHeight * 1.9f);
            const float half = jitter(index + 7, spacing * 0.55f, spacing * 0.95f);

            Entity hill = world.createEntity();
            world.addComponent(hill, Transform{x + jitter(index + 3, -30.0f, 30.0f),
                                               kGroundY, 0.0f});

            Polygon shape;
            shape.points = {Vec2{-half, 0.0f}, Vec2{0.0f, -height},
                            Vec2{half, 0.0f}};
            shape.closed = true;
            shape.r = r;
            shape.g = g;
            shape.b = b;
            shape.layer = layer;
            shape.parallax = parallax;
            world.addComponent(hill, shape);

            field_.push_back(hill);
        }
    }

    // Tufts in front of the fighting, sliding past faster than the ground.
    // Kept low and sparse: this layer is depth, not decoration to look at, and
    // anything taller would hide the thing the player is actually watching.
    void buildForeground(World& world) {
        const float width = bandWidth(kForeParallax) + 190.0f;
        for (float x = 0.0f; x < width; x += 190.0f) {
            const int index = static_cast<int>(x / 190.0f);
            const float height = jitter(index + 91, 10.0f, 22.0f);

            // Above the spawn bar, not at the bottom of the window.
            //
            // These sat at kWindowHeight - 6, which is six pixels from the
            // bottom edge — and the spawn bar covers everything from 480 down.
            // So the foreground band was drawn into a sliver nobody could see.
            // That band is the entire reason slice 6 pulled `parallax` out of
            // the engine: it is the one layer that moves FASTER than the
            // ground, which is the half of depth a boolean could never
            // express, and it has been invisible ever since.
            //
            // Between the unit line and the bar, so it reads as ground cover
            // in front of the fight without covering the fight itself.
            const float baseY = kButtonY - 28.0f;

            Entity tuft = world.createEntity();
            world.addComponent(tuft, Transform{x + jitter(index + 53, -40.0f, 40.0f),
                                               baseY, 0.0f});

            Polygon blades;
            blades.points = {Vec2{-9.0f, 0.0f},  Vec2{-4.0f, -height},
                             Vec2{-1.0f, 0.0f},  Vec2{3.0f, -height * 1.2f},
                             Vec2{6.0f, 0.0f},   Vec2{10.0f, -height * 0.8f}};
            blades.closed = false;
            // Green, and light enough to see. It was 30/38/44 — a blue-grey
            // within a few points of the ground behind it — so even once it
            // was drawn somewhere visible it still wasn't.
            blades.r = 64;
            blades.g = 92;
            blades.b = 68;
            blades.layer = kForeLayer;
            blades.parallax = kForeParallax;
            world.addComponent(tuft, blades);

            field_.push_back(tuft);
        }
    }

    // Posts along the field at a fixed spacing.
    //
    // These are not decoration. A uniform ground bar under a moving camera
    // looks exactly like a stationary ground bar under a stationary camera —
    // with nothing at a fixed world position to slide past, a correct camera
    // and a broken one are indistinguishable. Something has to mark distance
    // for the scrolling to read as scrolling at all.
    void buildDistanceMarkers(World& world) {
        for (float x = 240.0f; x < kWorldWidth - 120.0f; x += 240.0f) {
            field_.push_back(createRect(world, x, kGroundY - 46.0f, 3.0f, 46.0f,
                                        52, 58, 70, kFieldLayer));
            field_.push_back(createRect(world, x - 6.0f, kGroundY - 52.0f, 15.0f,
                                        6.0f, 62, 70, 84, kFieldLayer));
        }
    }

    void clearField(World& world) {
        for (Entity entity : world.entities()) {
            // Figures go explicitly rather than being left to the orphan
            // sweep: onExit is the one path where no further frame runs, so
            // there would be nothing left to sweep them.
            // Corpses and combat effects go with the battle too. Left behind,
            // they would lie across the stage list — the same bug, pointing
            // the other way, as the stage list once drawn over the fight.
            if (world.hasComponent<Unit>(entity) ||
                world.hasComponent<Shard>(entity) ||
                world.hasComponent<Cannonball>(entity) ||
                world.hasComponent<Figure>(entity) ||
                isBattleDressing(world, entity)) {
                world.destroyLater(entity);
            }
        }
        for (Entity entity : field_) world.destroyLater(entity);
        field_.clear();
    }

    static void earnGold(Session& session, float dt) {
        session.gold += goldPerSecondFor(session, true) * dt;
        session.enemyGold +=
            goldPerSecondFor(session, false) * session.enemyIncome * dt;
    }

    // --- Upgrades ----------------------------------------------------------

    void handleUpgrades(World& world, Session& session, InputManager& input) {
        if (input.wasMousePressed()) {
            const int index = upgradeAt(static_cast<float>(input.mouseX()),
                                        static_cast<float>(input.mouseY()));
            if (index >= 0) buyUpgrade(world, session, true, index);
        }

        // The opponent buys too, and has to: an enemy that cannot upgrade
        // loses every long game by construction, which is the same shape of
        // asymmetry that made slice 1 unwinnable and slice 3 a mirror.
        //
        // Its rule is "buy when you can afford it and still field your next
        // wave", which keeps it spending on units first — the mistake a human
        // makes here is over-investing, and an opponent that made it would be
        // free to beat.
        const int cheapest = cheapestUpgradeFor(session, false);
        if (cheapest >= 0 &&
            session.enemyGold >=
                upgradeCost(cheapest, session.enemyUpgrades[cheapest]) +
                    waveCost(session)) {
            buyUpgrade(world, session, false, cheapest);
        }
    }

    static int cheapestUpgradeFor(const Session& session, bool leftSide) {
        const int* owned = leftSide ? session.upgrades : session.enemyUpgrades;
        int best = -1;
        float bestCost = 0.0f;
        for (int index = 0; index < kUpgradeCount; ++index) {
            const float cost = upgradeCost(index, owned[index]);
            if (best < 0 || cost < bestCost) {
                best = index;
                bestCost = cost;
            }
        }
        return best;
    }

    void buyUpgrade(World& world, Session& session, bool leftSide, int index) {
        int* owned = leftSide ? session.upgrades : session.enemyUpgrades;
        float& purse = leftSide ? session.gold : session.enemyGold;

        const float cost = upgradeCost(index, owned[index]);
        if (purse < cost) return;

        purse -= cost;
        ++owned[index];

        // WALLS is the one upgrade with an immediate effect rather than a
        // rate: raising the maximum is worth nothing to a castle that is
        // already damaged, so the new stonework is healed on straight away.
        if (index == static_cast<int>(Upgrade::Walls)) {
            const Entity castle = findCastle(world, leftSide);
            if (Castle* health = world.getComponent<Castle>(castle)) {
                health->health += upgradeKind(index).effect;
            }
        }

        if (audioDevice) audioDevice->play(Waveform::Sine, 520.0f, 0.10f, 0.14f);
    }

    void handleSpawning(World& world, Session& session, InputManager& input,
                        float dt) {
        handlePlayerSpawning(world, session, input, dt);
        handleEnemySpawning(world, session, dt);
    }

    void handlePlayerSpawning(World& world, Session& session,
                              InputManager& input, float dt) {
        // Every kind's timer runs down every frame, whether or not anything is
        // sent — a cooldown that only ticked while you were pressing buttons
        // would punish you for waiting.
        tickCooldowns(session.spawnCooldowns, dt);

        if (countUnits(world, true) >= populationCapFor(session, true)) return;

        // One key per SLOT of the spawn bar, not per row of the roster.
        //
        // Those were different lists, and the difference was a bug waiting on
        // a data file. The bar skips the hero and stops at the window's edge;
        // the keys did neither, so they counted rows the bar had already
        // passed over. With the built-in roster the two happened to agree —
        // and a file adding a sixth unit type would have produced one the bar
        // was too short to show and the keys had already run out of, leaving
        // it purchasable by nothing at all.
        //
        // Asking `kindForButton` means there is now one answer to "what can
        // the player send", and the keyboard and the mouse cannot drift apart
        // again. `visibleButtonCount()` is bounded by kMaxVisibleButtons,
        // which is five, which is why there are five keys.
        static const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2,
                                            SDL_SCANCODE_3, SDL_SCANCODE_4,
                                            SDL_SCANCODE_5};
        constexpr int kKeyCount = static_cast<int>(sizeof(keys) / sizeof(keys[0]));
        static_assert(kKeyCount >= kMaxVisibleButtons,
                      "every slot the bar shows needs a key to reach it");

        for (int slot = 0; slot < visibleButtonCount() && slot < kKeyCount; ++slot) {
            if (!input.isKeyDown(keys[slot])) continue;
            const int kind = kindForButton(slot);
            if (kind >= 0 && trySpawn(world, session, kind)) return;
        }

        // The hero: one summon a battle, on its own key and its own button.
        // Not part of the number-key loop above, because it is not bought and
        // has no cooldown — the only thing standing between you and it is
        // whether you have already used it.
        if (input.wasKeyPressed(SDL_SCANCODE_H)) {
            summonHero(world, session);
        }

        // A click on the bar does exactly what its key does. `wasMousePressed`
        // rather than `isMouseDown`, or one held click would empty the purse
        // one unit per cooldown for as long as the finger stayed down.
        if (!input.wasMousePressed()) return;

        const float mouseX = static_cast<float>(input.mouseX());
        const float mouseY = static_cast<float>(input.mouseY());

        if (heroButtonHit(mouseX, mouseY)) {
            summonHero(world, session);
            return;
        }

        const int kind = buttonAt(mouseX, mouseY);
        if (kind >= 0) trySpawn(world, session, kind);
    }

    // Folds a path and everything bought within it into four numbers and a
    // flag, so nothing in the battle has to know what a path is.
    void readHeroPath(const Campaign& campaign) {
        const int path = std::min(std::max(0, campaign.heroPath),
                                  kHeroPathCount - 1);
        const HeroPathKind& kind = heroPath(path);

        heroPath_ = path;
        heroHealthScale_ = kind.health;
        heroPathDamage_ = kind.damage;
        heroDelayScale_ = kind.attackDelay;
        heroHitsAir_ = kind.hitsAir;
        heroHealPerSecond_ = kind.healPerSecond;

        for (int up = 0; up < kHeroUpgradesPerPath; ++up) {
            const HeroUpgradeKind& row = kind.upgrades[up];
            const int owned = std::min(campaign.heroUpgrades[path][up],
                                       row.maxLevel);
            const float total = static_cast<float>(owned) * row.effect;
            switch (row.stat) {
                case HeroStat::Health: heroHealthScale_ *= 1.0f + total; break;
                case HeroStat::Damage: heroPathDamage_  *= 1.0f + total; break;
                // A SMALLER gap between blows is faster, so this one subtracts.
                // Floored well above zero: a delay of nought would be an
                // infinite rate, and three levels of a data-driven percentage
                // is exactly the sort of thing that reaches nought by accident.
                case HeroStat::Delay:
                    heroDelayScale_ *= std::max(0.35f, 1.0f - total);
                    break;
                case HeroStat::Heal: heroHealPerSecond_ += total; break;
            }
        }
    }

    // The chaplain's aura: friendly units near the hero are mended while it
    // lives.
    //
    // Capped at each unit's own maximum, the same rule HEAL learned the hard
    // way — the hero's own maximum is raised by its path, so a cap read from
    // the roster would have healed it downwards.
    void healAroundTheHero(World& world, float dt) {
        if (heroHealPerSecond_ <= 0.0f) return;

        float heroX = 0.0f;
        float heroY = 0.0f;
        bool found = false;
        for (auto& [entity, hero] : world.view<Hero>()) {
            (void)hero;
            if (Transform* at = world.getComponent<Transform>(entity)) {
                heroX = at->x;
                heroY = at->y;
                found = true;
            }
        }
        if (!found) return;  // not summoned, or fallen

        const float mend = heroHealPerSecond_ * dt;
        for (auto& [entity, unit] : world.view<Unit>()) {
            Team* team = world.getComponent<Team>(entity);
            Transform* at = world.getComponent<Transform>(entity);
            if (!team || !at || !team->leftSide) continue;
            if (unit.health <= 0.0f) continue;  // already dead this frame

            const float dx = at->x - heroX;
            const float dy = at->y - heroY;
            if (dx * dx + dy * dy > kHeroAuraRadius * kHeroAuraRadius) continue;

            unit.health = std::min(unit.health + mend, unit.maxHealth);
        }
    }

    // Puts the hero on the field, once.
    //
    // No cost and no cooldown: the only limit is that there is exactly one of
    // them per battle. That makes the question purely *when* — early and it
    // fights alone and dies for nothing, late and the field it was meant to
    // win may already be lost.
    void summonHero(World& world, Session& session) {
        const int kind = heroKindIndex();
        if (kind < 0) return;
        if (session.heroSummoned) return;
        if (countUnits(world, true) >= populationCapFor(session, true)) return;

        session.heroSummoned = true;

        // Who was already out there to fight in front of it. Counted before it
        // arrives, so the hero is not its own escort.
        session.record.heroSummonedAt = session.elapsed;
        session.record.heroEscort = countUnits(world, true);

        const Entity hero = spawnUnit(world, true, kind);
        world.addComponent(hero, Hero{});

        // The hero looks like the path it walks, when there is art for it. A
        // WARDEN in the FALCONER's feathers would be telling the player
        // something false about what this hero can reach.
        if (heroPath_ > 0 && heroPath_ < kHeroPathCount) {
            if (const Unit* unit = world.getComponent<Unit>(hero)) {
                reskinFigure(world, unit->figure, unitKind(kind).pathSheet[heroPath_]);
            }
        }

        // CHAMPION is permanent, so it is applied on the way out of the gate
        // rather than to the roster: the table stays the hero everyone has,
        // and the perk is what this particular player has made of it.
        //
        // The PATH is applied the same way and on top: a multiplier from the
        // path itself, then whatever levels have been bought within it. So the
        // roster row stays "the hero everybody starts with" and everything
        // after it is what this player has made of theirs.
        const float champion =
            1.0f + perks_[static_cast<int>(Perk::Champion)] *
                       perkKind(static_cast<int>(Perk::Champion)).effect;

        if (Unit* unit = world.getComponent<Unit>(hero)) {
            unit->health *= champion * heroHealthScale_;
            // Its ceiling moves with it, or HEAL would clamp the hero back
            // down to the roster's number the first time it was cast.
            unit->maxHealth *= champion * heroHealthScale_;

            // The whole point of a path: two of the three cannot touch a
            // griffin, whatever the roster row says.
            unit->hitsAir = heroHitsAir_;
        }
        // Assigned from the two inputs rather than multiplied into itself, so
        // that a second call could not compound it. `heroSummoned` already
        // makes a second call impossible; this makes it harmless too.
        heroDamageScale_ = champion * heroPathDamage_;

        if (audioDevice) audioDevice->play(Waveform::Sine, 180.0f, 0.35f, 0.20f);
    }

    // Buys one unit if it can be afforded, and reports whether it did. The one
    // place gold is spent, so the keyboard and the mouse cannot drift apart.
    bool trySpawn(World& world, Session& session, int kind) {
        if (kind < 0 || kind >= unitKindCount()) return false;
        if (kind >= kMaxUnitKinds) return false;
        if (session.spawnCooldowns[kind] > 0.0f) return false;
        if (session.gold < unitKind(kind).cost) return false;

        session.gold -= unitKind(kind).cost;
        session.spawnCooldowns[kind] = cooldownFor(kind);
        spawnUnit(world, true, kind);
        return true;
    }

    static void tickCooldowns(float* cooldowns, float dt) {
        for (int kind = 0; kind < kMaxUnitKinds; ++kind) {
            if (cooldowns[kind] > 0.0f) cooldowns[kind] -= dt;
        }
    }

    // Floored, so a data file setting a cooldown of zero cannot turn one held
    // key into an army in a single frame.
    static float cooldownFor(int kind) {
        return std::max(kMinSpawnCooldown, unitKind(kind).cooldown);
    }

    // The opponent, playing by the same rules from the same purse.
    //
    // It banks for a whole wave before spending any of it, because measuring
    // slice 2 showed that spending on sight loses to anyone who doesn't: the
    // two front lines mirror each other and nothing ever moves. Now that both
    // sides bank, the player has to beat it on composition instead.
    void handleEnemySpawning(World& world, Session& session, float dt) {
        // The opponent lives under the same per-kind cooldowns. Symmetry here
        // has been load-bearing three times now: an opponent that could pour a
        // whole purse into one unit type while the player could not would be
        // playing a different game, and a better one.
        tickCooldowns(session.enemySpawnCooldowns, dt);

        // Past the clock their castle stops paying for an army: the swarm is
        // the army. See beginSwarm for why it cannot be both.
        if (session.swarming) return;

        if (session.enemyWaveRemaining <= 0) {
            if (session.enemyGold >= waveCost(session)) {
                session.enemyWaveRemaining = session.enemyWaveSize;
            }
            return;
        }

        if (countUnits(world, false) >= populationCapFor(session, false)) return;

        // Reads FORWARD through the composition for something it can actually
        // send, rather than waiting on whatever came next.
        //
        // Taking only the next entry looked right and throttled the opponent
        // badly: its cycle opens with two soldiers, so the second one sat
        // waiting out the first one's cooldown instead of sending the archer
        // behind it. Per-kind cooldowns only reward you for diversifying if
        // you are allowed to diversify.
        for (int step = 0; step < session.compositionLength; ++step) {
            const int index =
                (session.enemyWaveIndex + step) % session.compositionLength;
            const int kind = session.composition[index];

            if (kind >= kMaxUnitKinds) continue;
            if (session.enemySpawnCooldowns[kind] > 0.0f) continue;
            if (session.enemyGold < unitKind(kind).cost) continue;

            session.enemyGold -= unitKind(kind).cost;
            session.enemySpawnCooldowns[kind] = cooldownFor(kind);
            session.enemyWaveIndex = (index + 1) % session.compositionLength;
            --session.enemyWaveRemaining;
            spawnUnit(world, false, kind);
            return;
        }
    }

    // What the next whole wave costs, reading forward through the cycle from
    // wherever it currently is.
    static float waveCost(const Session& session) {
        float total = 0.0f;
        for (int step = 0; step < session.enemyWaveSize; ++step) {
            const int index = (session.enemyWaveIndex + step) %
                              session.compositionLength;
            total += unitKind(session.composition[index]).cost;
        }
        return total;
    }

    // --- The swarm ---------------------------------------------------------
    //
    // Ten minutes without a castle falling, and theirs stops paying for its
    // army. See kBattleSeconds for why the swarm is theirs and not a tie-break.

    // The moment the clock runs out. The record is copied first, so a defeat
    // can be explained by the ten minutes that did not break through rather
    // than by the swarm that followed.
    //
    // And their army JOINS the swarm: everything of theirs already on the
    // field is marked, and grows with it, and from here the swarm is the only
    // way they field anything (handleEnemySpawning stops). That was the third
    // version's hole. A ten-minute stalemate is ten minutes of the opponent
    // banking bounties into SUPPLY, three slots a level, and by the clock an
    // ordinary army of twenty-odd could fill the field to the swarm's ceiling
    // on its own — so the swarm could not get a unit out, the front of their
    // line never grew, and one army held for five minutes more.
    void beginSwarm(World& world, Session& session) {
        session.swarming = true;
        session.beforeSwarm = session.record;
        session.swarmTimer = 0.0f;  // the first one comes out at once
        for (auto& [entity, unit] : world.view<Unit>()) {
            const Team* team = world.getComponent<Team>(entity);
            if (team && !team->leftSide) unit.swarm = true;
        }
        if (audioDevice) audioDevice->play(Waveform::Square, 98.0f, 0.8f, 0.22f);
    }

    // One unit out of the gate whenever one is due, from the stage's own
    // composition — so THE EYRIE's swarm flies and THE GATES' marches — free of
    // gold, cooldowns and the population cap, and stronger the later it comes:
    // more health AND harder blows, because only the front of a queue fights
    // (see kSwarmStrengthenSeconds).
    void updateSwarm(World& world, Session& session, float dt) {
        if (!session.swarming) return;

        const float into = session.elapsed - kBattleSeconds;
        const float strength = swarmStrength(into);

        // EVERY swarm unit on the field grows with the swarm — not only each
        // one as it comes out of the gate, which was the second version and
        // still let three stalemates stand for five minutes. Units queue, and
        // with thirty in the column the one at the front, the only one
        // fighting, left the gate thirty kills earlier: a generation behind the
        // formula, falling further behind as each kill took longer. Growing
        // them in place keeps the front of the queue as strong as the swarm.
        // Health grows by the same factor as the blows, so a wounded one stays
        // exactly as wounded.
        for (auto& [entity, body] : world.view<Unit>()) {
            if (!body.swarm || body.power >= strength) continue;
            const float grow = strength / body.power;
            body.health *= grow;
            body.maxHealth *= grow;
            body.power = strength;
        }

        session.swarmTimer -= dt;
        if (session.swarmTimer > 0.0f) return;

        // Room on the field or nothing. The timer stays due, so the next unit
        // comes out the moment one of theirs falls.
        if (countUnits(world, false) >= kSwarmCeiling) return;

        // And room at the gate: one at a time, through a gate the last one has
        // walked clear of. Dropped on top of a unit already standing there, two
        // bodies share one spot and walk as one, which reads as a single unit
        // however many there are — the swarm should LOOK like a crowd coming.
        const int length = std::max(1, session.compositionLength);
        const int kind = session.composition[session.swarmIndex % length];
        if (!gateClear(world, kind)) return;

        // Added to what is left over rather than assigned, like a unit's
        // attack timer, so the rate means units per SECOND at any frame rate;
        // and floored at zero so a frame never owes more than one — a long
        // wait at the ceiling must not come out of the gate all at once.
        session.swarmTimer += swarmGap(into);
        if (session.swarmTimer < 0.0f) session.swarmTimer = 0.0f;
        session.swarmIndex = (session.swarmIndex + 1) % length;

        const Entity unit = spawnUnit(world, false, kind);
        if (Unit* body = world.getComponent<Unit>(unit)) {
            body->swarm = true;
            body->health *= strength;
            body->maxHealth *= strength;
            body->power = strength;
        }

        // spawnUnit counted it as their ARMY. It is the swarm.
        if (kind >= 0 && kind < kMaxUnitKinds) --session.record.enemySent[kind];
        ++session.record.swarmSent;
    }

    // Whether a unit of `kind` coming out of their gate would stand clear of
    // everything of theirs already in its lane — a rank's width apart, the
    // same room a queue leaves.
    static bool gateClear(World& world, int kind) {
        const UnitKind& stats = kindOf(kind);
        const float spawnCentre = kRightSpawnX + stats.width / 2.0f;
        for (auto& [entity, unit] : world.view<Unit>()) {
            const Team* team = world.getComponent<Team>(entity);
            const Transform* at = world.getComponent<Transform>(entity);
            if (!team || team->leftSide || !at) continue;
            const UnitKind& other = kindOf(unit.kind);
            if (other.flying != stats.flying) continue;  // the sky is its own lane
            const float apart = std::fabs(at->x + other.width / 2.0f - spawnCentre);
            if (apart < (stats.width + other.width) / 2.0f + kRankGap) return false;
        }
        return true;
    }

    // Damage done to you, credited to the kind that did it — which is how the
    // defeat screen can say what beat you rather than only that something did.
    static void recordHarm(Session& session, int kind, float damage) {
        if (kind >= 0 && kind < kMaxUnitKinds) {
            session.record.damageTakenFrom[kind] += damage;
        }
    }

    // Each unit either walks or fights, never both.
    void fight(World& world, Session& session, float dt) {
        // Gathered first, because attacking creates shards and killing blows
        // queue deletions — neither of which should happen while iterating
        // the live entity list.
        //
        // Gathered from `entities()` rather than from `view<Unit>()`, which
        // would be the faster pool and is the wrong one HERE. entities() is a
        // vector in creation order; the pool is a hash map whose order is a
        // standard-library detail. Order does not matter to a count or to a
        // coordinate — which is why the queries above could move — but it
        // decides who swings first when two blows land in the same frame, and
        // therefore which of two units dies. Taking the cheaper pool would buy
        // a fraction of a percent of one frame and pay for it with a battle
        // that can resolve differently on Linux than on Windows.
        std::vector<Entity> units;
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Unit>(entity)) units.push_back(entity);
        }

        for (Entity attacker : units) {
            Unit* unit = world.getComponent<Unit>(attacker);
            Team* team = world.getComponent<Team>(attacker);
            Transform* at = world.getComponent<Transform>(attacker);
            Velocity* velocity = world.getComponent<Velocity>(attacker);
            if (!unit || !team || !at || !velocity) continue;

            // The dead do not swing. They can still be here: a spell or a
            // shell kills after removeTheDead has run for this frame, so the
            // body is cleared on the next pass — and until this line existed
            // it got a free attack on the way out. A unit at zero health that
            // still deals its damage makes killing something worth less than
            // the arithmetic says, and makes it depend on the order the blow
            // arrived in.
            if (unit->health <= 0.0f) continue;

            const UnitKind& stats = kindOf(unit->kind);
            const Entity target =
                findTargetAhead(world, attacker, team->leftSide, at->x, stats,
                                unit->hitsAir);

            if (target == kInvalidEntity) {
                // Nothing in reach. March, unless one of our own is in the
                // way — in which case wait our turn rather than standing
                // inside them.
                //
                // The swarm does not wait its turn. It is a mob, not a line:
                // everything in it presses forward until something is in
                // reach, so it arrives at your front all at once instead of
                // one queue-length at a time. Measured, not only named: in a
                // queue only the front unit ever fights, however many pour
                // out of the gate, and a swarm that queued took minutes to do
                // what a mob does in seconds.
                const bool queued = !unit->swarm &&
                                    blockedByFriendly(world, attacker, team->leftSide,
                                                      at->x, stats);
                velocity->dx = queued ? 0.0f : stats.speed * facing(team->leftSide);
                continue;
            }

            velocity->dx = 0.0f;
            unit->timeUntilAttack -= dt;
            if (unit->timeUntilAttack > 0.0f) continue;

            // The hero's path can shorten the gap between its blows. Everything
            // else swings at whatever the roster says.
            const float delay = world.hasComponent<Hero>(attacker)
                                    ? stats.attackDelay * heroDelayScale_
                                    : stats.attackDelay;

            // ADDED to what is left over, not assigned. The timer is normally
            // slightly PAST zero when a swing lands — a frame is not going to
            // end exactly on the interval — and assigning the full delay threw
            // that overshoot away, once per swing. It made a unit's real rate
            // of fire depend on the frame rate: a soldier hitting a wall for a
            // minute dealt 1,260 damage at 15 frames a second and 1,400 at
            // sixty, because at 15 it lost a bigger slice each time. Two
            // players on different machines were playing measurably different
            // games, and none of the balance measured by the probe applied to
            // either of them.
            //
            // Banking the remainder makes the interval mean seconds. Same
            // reasoning as the while-loop in AnimationSystem.
            unit->timeUntilAttack += delay;

            // A single frame may owe a single swing, never a backlog. Without
            // this a long stall — a breakpoint, a window drag, a loading
            // hitch — would leave the timer far enough behind that the unit
            // fired every frame for a while to catch up, turning a hiccup into
            // a burst of damage nobody asked for.
            if (unit->timeUntilAttack < 0.0f) unit->timeUntilAttack = 0.0f;
            unit->swing = 1.0f;  // starts the arm through its arc

            // WEAPONS is the player's perk, so only the player's units swing
            // harder for it. Scaling every blow would have handed the
            // opponent every upgrade the player ever bought.
            //
            // CHAMPION stacks on top for the hero alone, which is what makes
            // it worth buying separately from WEAPONS.
            float damage = stats.damage * unit->power;
            if (team->leftSide) {
                damage *= damageScale_;
                // And whatever this KIND has been trained to, which is why a
                // levelled soldier is worth more than a levelled purse.
                damage *= trainedDamage(unit->kind, trainingLevel(unit->kind));
                if (world.hasComponent<Hero>(attacker)) damage *= heroDamageScale_;

                // RAGE is temporary and applies to everything you own,
                // including the hero — which is what makes casting it while
                // the hero is out worth more than casting it any other time.
                if (session.rageSeconds > 0.0f) damage *= spellKind(2).power;
            }

            if (Unit* victim = world.getComponent<Unit>(target)) {
                victim->health -= damage;
                if (!team->leftSide) recordHarm(session, unit->kind, damage);
                playHit();
            } else if (Castle* castle = world.getComponent<Castle>(target)) {
                castle->health -= damage;
                if (team->leftSide) {
                    session.record.castleDamageDealt += damage;
                } else {
                    recordHarm(session, unit->kind, damage);
                }
                playCastleHit();
                // Whether that ENDED the battle is not decided here. See
                // settleBattle, called once after the loop.
            }
            showBlow(world, attacker, target, stats, team->leftSide);
        }
    }

    // The picture of a blow: an arrow in flight, or a flash where it landed.
    //
    // After the damage, and never instead of it — this is decoration, and
    // spawnEffect declines far more often than it agrees (a chance per blow,
    // a limit per kind, a limit overall), so a big melee shows a few flashes
    // rather than burying the fight under thirty. With no texture cache it
    // does nothing at all, which is every test and both simulators.
    void showBlow(World& world, Entity attacker, Entity target,
                  const UnitKind& stats, bool leftSide) {
        if (!currentTextureCache() || !stats.blow) return;
        const Transform* from = world.getComponent<Transform>(attacker);
        const Transform* to = world.getComponent<Transform>(target);
        const Sprite* toSprite = world.getComponent<Sprite>(target);
        if (!from || !to || !toSprite) return;

        // Where it lands: the target's near edge, level with the attacker's
        // chest — or the middle of a flyer.
        const float direction = facing(leftSide);
        const float targetCentre = to->x + toSprite->width / 2.0f;
        const float nearEdge = targetCentre - direction * toSprite->width / 2.0f;
        const bool targetIsUnit = world.hasComponent<Unit>(target);
        const float hitY = targetIsUnit
                               ? to->y + toSprite->height * 0.45f
                               : from->y + stats.height * 0.45f;

        // A WARDEN takes blows on a shield, and should be seen to.
        std::string name = stats.blow;
        if (world.hasComponent<Hero>(target) && heroPath_ == static_cast<int>(HeroPath::Warden)) {
            name = "SHIELD";
        }

        const EffectKind* kind = effectKind(name);
        if (!kind) return;
        if (kind->flies) {
            const float handX = from->x + stats.width / 2.0f + direction * stats.width * 0.4f;
            const float handY = from->y + stats.height * 0.4f;
            spawnProjectile(world, name, handX, handY, targetCentre, hitY);
        } else {
            spawnEffect(world, name, nearEdge, hitY, !leftSide);
        }
    }

    // Decides whether the battle is over, exactly once.
    //
    // This used to live inside the attacker loop, on the frame a castle's
    // health crossed zero, and it did three things there that only look
    // correct while one unit is in reach of a castle at a time:
    //
    //   - It paid the reward from inside a loop that keeps going. Three
    //     attackers finishing the same castle in one update ran the payout
    //     three times, banking double the intended first clear and saving each
    //     time.
    //   - It wrote `playerWon` from whichever castle happened to fall LAST in
    //     iteration order. With both castles on their final point of health it
    //     could pay out a win and then report a loss, in that order, in one
    //     update.
    //   - It ran before anything else that can damage a castle — the cannon
    //     and a bolt both can — so those had to wait a frame to be noticed.
    //
    // Asking the question once, after all of this update's damage has landed,
    // makes all three go away. The rule for the simultaneous case is stated
    // rather than emergent: YOUR castle falling is a loss even if theirs fell
    // in the same instant. You have to be standing at the end. The alternative
    // rewards ignoring defence entirely, and this game already pays well for
    // attacking.
    //
    // Only a castle ends a battle — the clock included. When time runs out it
    // sends the swarm (see updateSwarm) rather than a verdict, so a stalemate
    // is ended by the swarm breaking a castle, and this function never has to
    // know the clock exists.
    void settleBattle(World& world, Session& session) {
        if (session.gameOver) return;

        const Entity mine = findCastle(world, true);
        const Entity theirs = findCastle(world, false);
        const Castle* myCastle = world.getComponent<Castle>(mine);
        const Castle* theirCastle = world.getComponent<Castle>(theirs);

        const bool iFell = myCastle && myCastle->health <= 0.0f;
        const bool theyFell = theirCastle && theirCastle->health <= 0.0f;
        if (!iFell && !theyFell) return;

        session.gameOver = true;
        session.playerWon = theyFell && !iFell;
        if (!session.playerWon) return;

        // Winning opens the next stage. Clamped to the last one, so finishing
        // the campaign does not leave the list pointing past its own end.
        Campaign& campaign = campaignOf(world);
        campaign.stagesUnlocked = std::min(
            stageCount(), std::max(campaign.stagesUnlocked, stage_ + 2));

        // Paid out, and the first clear pays double — so pushing forward is
        // worth more than farming a stage already beaten, without ever
        // forbidding the farming.
        const bool firstClear = stage_ < kMaxStages && !campaign.cleared[stage_];
        campaign.bank += static_cast<int>(stageReward(stage_, firstClear));
        if (stage_ < kMaxStages) campaign.cleared[stage_] = true;

        // Saved the moment it is earned rather than on exit. A campaign lost
        // because the window was closed the wrong way is a bad way to learn
        // about save points.
        saveCampaign(campaign);
    }

    void removeTheDead(World& world, Session& session) {
        // The dead are gathered BEFORE any of them is buried.
        //
        // This used to walk `world.entities()` directly and call spawnShards
        // from inside the loop — and spawnShards creates six entities, each of
        // which push_backs onto the very vector being iterated. ECS.h says in
        // as many words that entities() is the live list and that creating an
        // entity while looping it can reallocate the vector under the loop.
        //
        // It had been there since slice 1 and only ever mattered when a push
        // happened to cross a capacity boundary during a death, which is why
        // it took an unrelated test — a chaplain healing a line while a fight
        // was killing it — to fall over. `fight` already gathers first for
        // exactly this reason; this is the one place that did not.
        std::vector<Entity> dead;
        for (auto& [entity, unit] : world.view<Unit>()) {
            if (unit.health <= 0.0f) dead.push_back(entity);
        }

        for (Entity entity : dead) {
            Unit* unit = world.getComponent<Unit>(entity);
            if (!unit) continue;

            // Whoever killed it gets paid. The bounty is a share of what the
            // casualty cost its owner, so trading cheap units for expensive
            // ones is profitable and trading the other way is not.
            const float bounty = kindOf(unit->kind).cost * kKillRewardFraction;
            if (Team* team = world.getComponent<Team>(entity)) {
                if (team->leftSide) {
                    session.enemyGold += bounty;
                } else {
                    session.gold += bounty;
                }
                if (unit->kind >= 0 && unit->kind < kMaxUnitKinds) {
                    ++(team->leftSide ? session.record.lost : session.record.killed)[unit->kind];
                }
            }

            // A unit with artwork leaves its death row playing where it fell;
            // one without scatters shards, as it always did. Either way the
            // unit itself is gone this frame — the corpse is a picture, not a
            // combatant, so nothing about the fight waits for it.
            const bool hasArt = unit->figure != kInvalidEntity &&
                                world.hasComponent<ArtFigure>(unit->figure);
            if (hasArt) {
                leaveCorpse(world, unit->figure);
            } else if (Transform* at = world.getComponent<Transform>(entity)) {
                const UnitKind& stats = kindOf(unit->kind);
                Sprite* sprite = world.getComponent<Sprite>(entity);
                spawnShards(world, at->x + stats.width / 2.0f,
                            at->y + stats.height / 2.0f,
                            sprite ? sprite->r : 200, sprite ? sprite->g : 200,
                            sprite ? sprite->b : 200);
            }
            // The hero falling is remembered, because it is the whole rule:
            // no respawn, no second summon, not until the stage is finished
            // or started again.
            if (world.hasComponent<Hero>(entity)) {
                session.heroFallen = true;
                session.record.heroFellAt = session.elapsed;
            }

            playDeath();
            world.destroyLater(entity);
            if (unit->figure != kInvalidEntity) world.destroyLater(unit->figure);
        }
    }

    // --- Spells ------------------------------------------------------------

    void updateSpells(World& world, Session& session, InputManager& input,
                      float dt) {
        session.mana = std::min(kMaxMana, session.mana + kManaPerSecond * dt);
        session.rageSeconds = std::max(0.0f, session.rageSeconds - dt);

        static const SDL_Scancode keys[] = {SDL_SCANCODE_Z, SDL_SCANCODE_X,
                                            SDL_SCANCODE_C};
        for (int spell = 0; spell < kSpellCount; ++spell) {
            if (input.wasKeyPressed(keys[spell])) selectSpell(world, session, spell);
        }

        if (input.wasMousePressed()) {
            const int clicked = spellAt(static_cast<float>(input.mouseX()),
                                        static_cast<float>(input.mouseY()));
            if (clicked >= 0) selectSpell(world, session, clicked);
        }

        // Right-click, or pressing the armed spell again, puts it away. An
        // armed spell steals the next click from the cannon, so there has to
        // be a way out that is not "cast it somewhere useless".
        if (input.wasMousePressed(SDL_BUTTON_RIGHT)) session.armedSpell = -1;
    }

    // Arms an aimed spell, or casts an unaimed one on the spot.
    void selectSpell(World& world, Session& session, int spell) {
        if (spell < 0 || spell >= kSpellCount) return;

        // An unaimed spell goes straight to castSpell, which is the ONE place
        // mana is checked and spent. Checking here as well was harmless and
        // useless: it made the real guard unreachable, so breaking it changed
        // nothing any test could see.
        if (!spellKind(spell).aimed) {
            castSpell(world, session, spell, 0.0f, 0.0f);
            return;
        }

        // Arming is gated separately, because arming something you cannot pay
        // for would take the next click and then do nothing with it.
        if (session.mana < spellKind(spell).manaCost) return;
        session.armedSpell = (session.armedSpell == spell) ? -1 : spell;
    }

    void castSpell(World& world, Session& session, int spell, float worldX,
                   float worldY) {
        const SpellKind& kind = spellKind(spell);
        if (session.mana < kind.manaCost) return;

        session.mana -= kind.manaCost;
        session.armedSpell = -1;

        switch (static_cast<Spell>(spell)) {
            case Spell::Meteor: castMeteor(world, kind, worldX, worldY); break;
            case Spell::Heal:   castHeal(world, kind, worldX, worldY);   break;
            case Spell::Rage:
                session.rageSeconds = kind.duration;
                glowOver(world, true, 0.0f, 0.0f, 0.0f, "RAGE");  // radius 0: everyone
                break;
            default: break;
        }

        if (audioDevice) {
            audioDevice->play(Waveform::Sine, 300.0f + 90.0f * spell, 0.30f, 0.18f);
        }
    }

    // Both aimed spells ask the same question — which units are within a
    // radius of this point — and differ only in what they do to the answer.
    void forUnitsNear(World& world, bool leftSide, float x, float y,
                      float radius, const std::function<void(Unit&)>& action) {
        for (auto& [entity, unit] : world.view<Unit>()) {
            Team* team = world.getComponent<Team>(entity);
            Transform* at = world.getComponent<Transform>(entity);
            if (!team || !at || team->leftSide != leftSide) continue;

            const UnitKind& stats = kindOf(unit.kind);
            const float dx = (at->x + stats.width / 2.0f) - x;
            const float dy = (at->y + stats.height / 2.0f) - y;
            if (dx * dx + dy * dy > radius * radius) continue;

            action(unit);
        }
    }

    void castMeteor(World& world, const SpellKind& kind, float x, float y) {
        const float power = kind.power;
        forUnitsNear(world, false, x, y, kind.radius,
                     [power](Unit& unit) { unit.health -= power; });

        for (int piece = 0; piece < 3; ++piece) {
            spawnShards(world, x + static_cast<float>(piece - 1) * 30.0f, y,
                        255, 180, 90);
        }
        spawnEffect(world, "METEOR", x, y - 20.0f, false);
    }

    // Healing is capped at what the unit started with: a heal that overfilled
    // would make a wounded veteran better than a fresh one, and turn the spell
    // into a permanent stat upgrade you cast repeatedly.
    //
    // At the unit's OWN maximum, not at its roster row. The two are the same
    // number for everything the bar sells and differ for exactly one thing —
    // a hero carrying CHAMPION — which is why capping at the table looked
    // right for four slices and was quietly subtracting health from the one
    // unit a player had paid extra for.
    void castHeal(World& world, const SpellKind& kind, float x, float y) {
        const float power = kind.power;
        forUnitsNear(world, true, x, y, kind.radius, [power](Unit& unit) {
            unit.health = std::min(unit.health + power, unit.maxHealth);
        });

        spawnShards(world, x, y, 150, 255, 190);
        // A glow on each unit it reached — up to HEAL's limit, so a packed line
        // gets a handful rather than one per soldier.
        glowOver(world, true, x, y, kind.radius, "HEAL");
    }

    // An effect over every unit of one side within `radius` of a point. The
    // effect's own limit is what keeps a big blob of units from getting one
    // each; this does not have to count.
    void glowOver(World& world, bool leftSide, float x, float y, float radius,
                  const char* effect) {
        if (!currentTextureCache()) return;
        for (auto& [entity, unit] : world.view<Unit>()) {
            const Team* team = world.getComponent<Team>(entity);
            const Transform* at = world.getComponent<Transform>(entity);
            if (!team || !at || team->leftSide != leftSide) continue;
            const UnitKind& stats = kindOf(unit.kind);
            const float cx = at->x + stats.width / 2.0f;
            const float cy = at->y + stats.height / 2.0f;
            if (radius > 0.0f && (cx - x) * (cx - x) + (cy - y) * (cy - y) > radius * radius) {
                continue;
            }
            spawnEffect(world, effect, cx, cy, !leftSide);
        }
    }

    // --- The cannon --------------------------------------------------------

    // Gravity, and arrival. MovementSystem already applies the velocity; this
    // only bends it downward and decides when the shot has got where it was
    // aimed. Arrival is by clock rather than by position, because the launch
    // was solved for exactly this time — checking "has it reached the ground"
    // instead would mean a shot aimed at a hill never lands.
    void updateCannonballs(World& world, float dt) {
        std::vector<Entity> landed;

        for (auto& [entity, shot] : world.view<Cannonball>()) {
            Transform* at = world.getComponent<Transform>(entity);
            if (!at) continue;

            shot.elapsed += dt;

            // Clamped, so the last frame of the flight puts the shell exactly
            // on the point it was aimed at rather than however far past it the
            // frame happened to carry. That overshoot is up to a whole frame of
            // travel, which is the other half of the frame-rate dependence.
            const float t = std::min(shot.elapsed, kCannonFlightTime);
            at->x = shot.startX + shot.velocityX * t;
            at->y = shot.startY + shot.velocityY * t +
                    kCannonGravity * t * t * 0.5f;

            if (shot.elapsed >= kCannonFlightTime) landed.push_back(entity);
        }

        // Gathered first: exploding creates shards and queues deaths, neither
        // of which belongs in the middle of iterating the pool.
        for (Entity entity : landed) explode(world, entity);
    }

    void explode(World& world, Entity shot) {
        const Cannonball* payload = world.getComponent<Cannonball>(shot);
        const Transform* at = world.getComponent<Transform>(shot);
        if (!payload || !at) return;

        const bool firedByLeft = payload->leftSide;
        const float blastX = at->x;
        const float blastY = at->y;
        Session* session = findSession(world);

        for (auto& [entity, unit] : world.view<Unit>()) {
            Team* team = world.getComponent<Team>(entity);
            Transform* unitAt = world.getComponent<Transform>(entity);
            if (!team || !unitAt) continue;
            if (team->leftSide == firedByLeft) continue;  // never your own army

            const UnitKind& stats = kindOf(unit.kind);
            const float dx = (unitAt->x + stats.width / 2.0f) - blastX;
            const float dy = (unitAt->y + stats.height / 2.0f) - blastY;
            if (dx * dx + dy * dy > kCannonBlastRadius * kCannonBlastRadius) {
                continue;
            }
            unit.health -= kCannonDamage;
            if (session && !firedByLeft) session->record.cannonDamageTaken += kCannonDamage;
        }

        spawnShards(world, blastX, blastY, 250, 210, 140);
        // The painted burst on top of the shards, when there is art. The
        // shards stay: they are what a headless run and the old look share.
        spawnEffect(world, "CANNON", blastX, blastY - 10.0f, !firedByLeft);
        if (audioDevice) audioDevice->play(Waveform::Noise, 0.0f, 0.22f, 0.20f);
        world.destroyLater(shot);
    }

    // Where a side's cannon sits: the top of its own castle.
    static float cannonX(bool leftSide) {
        return (leftSide ? kLeftCastleX : kRightCastleX) + kCastleWidth / 2.0f;
    }
    static float cannonY() { return kGroundY - kCastleHeight; }

    // Clamped to the cannon's reach, so a click at the far end of the field
    // drops the shot at the edge of range rather than doing nothing. Silently
    // ignoring an out-of-range click reads as a broken button.
    void fireAt(World& world, Session& session, float worldX, float worldY) {
        if (session.cannonCooldown > 0.0f) return;
        if (session.gold < kCannonCost) return;

        const float from = cannonX(true);
        const float reach = std::min(std::max(worldX - from, -kCannonRange),
                                     kCannonRange);
        session.gold -= kCannonCost;
        session.cannonCooldown = kCannonCooldown;
        fireCannon(world, true, from, cannonY(), from + reach,
                   std::min(worldY, kGroundY));
    }

    // The enemy's cannon needs no aiming: it drops shots on wherever your
    // advance has reached, which is the same information the minimap shows you
    // and is exactly what a competent player would aim at.
    void updateEnemyCannon(World& world, Session& session, float dt) {
        session.enemyCannonCooldown -= dt;
        if (session.enemyCannonCooldown > 0.0f) return;

        const float from = cannonX(false);
        const float target = frontLineX(world, true);
        if (std::fabs(target - from) > kCannonRange) return;  // out of reach; wait

        // It shells only out of true surplus: after its next wave AND its next
        // upgrade are both covered.
        //
        // Reserving for the wave alone was not enough, and the failure was
        // quiet. A shot plus a wave came to about 200 gold and an upgrade plus
        // a wave to about 290, so the cheaper commitment always won the race
        // and the opponent never upgraded once in a four-hundred-second game.
        // It shelled away every surplus it ever had and stayed poor for the
        // whole match — losing at full health, which looked like balance and
        // was actually an ordering mistake.
        // Skipped entirely while the gun is free, so both sides shell on the
        // same terms.
        //
        // The reserve exists to stop the opponent spending its army on shells.
        // With a free cannon there is nothing to spend, and leaving the check
        // in place would have meant the PLAYER shelling from the first second
        // while the opponent waited until it could afford a wave and an
        // upgrade it no longer needed to pay for. Asymmetry in a mechanic both
        // sides own is the bug slice 1 and slice 3 were both lost to.
        //
        // `if constexpr` because kCannonCost is a compile-time constant, and a
        // plain `if` on one is a warning this project does not carry. The
        // branch stays rather than being deleted: the price has already been
        // set to zero twice during tuning, and this is the line that keeps the
        // opponent honest when it is.
        if constexpr (kCannonCost > 0.0f) {
            const int nextUpgrade = cheapestUpgradeFor(session, false);
            const float reserve =
                waveCost(session) +
                (nextUpgrade >= 0
                     ? upgradeCost(nextUpgrade, session.enemyUpgrades[nextUpgrade])
                     : 0.0f);
            if (session.enemyGold < kCannonCost + reserve) return;
        }

        session.enemyGold -= kCannonCost;
        session.enemyCannonCooldown = kCannonCooldown;
        fireCannon(world, false, from, cannonY(), target, kGroundY - 10.0f);
    }

    // Is this screen position on any panel that takes a click?
    //
    // One place, so a new panel is added here once rather than remembered in
    // every place that asks. Both bugs this replaced were of exactly that
    // shape: a panel added, and one of the two questions about it forgotten.
    static bool isOverUi(float screenX, float screenY) {
        return buttonAt(screenX, screenY) >= 0 ||
               upgradeAt(screenX, screenY) >= 0 ||
               spellAt(screenX, screenY) >= 0 ||
               heroButtonHit(screenX, screenY);
    }

    // --- The view ----------------------------------------------------------

    // Where the camera wants to be: your front line, centred, and never past
    // the ends of the world.
    float cameraTargetX(World& world) const {
        const float centred = frontLineX(world, true) -
                              static_cast<float>(kWindowWidth) / 2.0f;
        return std::min(std::max(centred, 0.0f), kCameraMaxX);
    }

    void snapCameraToTarget(World& world) {
        if (Camera* camera = world.getComponent<Camera>(cameraEntity_)) {
            camera->x = cameraTargetX(world);
        }
    }

    void updateCamera(World& world, Session& session, InputManager& input,
                      float dt) {
        Camera* camera = world.getComponent<Camera>(cameraEntity_);
        if (!camera) return;

        const float mouseX = static_cast<float>(input.mouseX());
        const float mouseY = static_cast<float>(input.mouseY());

        // One button, several verbs. A press on the field is undecided: move
        // more than a few pixels and it is a camera drag, release without
        // moving and it is a cannon shot — or a spell, if one is armed.
        //
        // A press that lands on a UI element is none of those. That test used
        // to name the spawn bar and the upgrade panel and nothing else, which
        // meant clicking the hero button also fired the cannon, and clicking a
        // spell row armed the spell AND immediately cast it into the panel.
        // Every new panel was one more chance to forget; asking one question
        // is what stops the next one being forgotten too.
        if (input.wasMousePressed() && !isOverUi(mouseX, mouseY)) {
            session.pressPending = true;
            session.dragging = false;
            session.dragStartX = mouseX;
            session.dragStartCameraX = camera->x;

            // Converted to world coordinates NOW, while the camera is still
            // where it was when the player took aim. Converting on release
            // instead would use whatever the camera had drifted to in the
            // meantime, and a held press drifts because following keeps
            // running underneath it.
            session.pressX = screenToWorldX(*camera, mouseX);
            session.pressY = screenToWorldY(*camera, mouseY);
        }

        if (session.pressPending && input.isMouseDown()) {
            // The field follows the cursor: drag left and the world moves
            // left under your finger, which means the camera moves right.
            const float moved = mouseX - session.dragStartX;
            if (std::fabs(moved) >= kDragThreshold) session.dragging = true;

            if (session.dragging) {
                camera->x = session.dragStartCameraX - moved;
                session.freeLookSeconds = kFreeLookHold;
                camera->x = std::min(std::max(camera->x, 0.0f), kCameraMaxX);
                camera->y = 0.0f;
                return;
            }
        }

        if (input.wasMouseReleased() && session.pressPending) {
            if (!session.dragging) {
                // An armed spell takes the click. The cannon only fires when
                // nothing else has claimed it, which is what lets one button
                // serve three verbs without a modifier key.
                if (session.armedSpell >= 0) {
                    castSpell(world, session, session.armedSpell, session.pressX,
                              session.pressY);
                } else {
                    fireAt(world, session, session.pressX, session.pressY);
                }
            }
            session.pressPending = false;
            session.dragging = false;
        }

        const bool left = input.isKeyDown(SDL_SCANCODE_LEFT);
        const bool right = input.isKeyDown(SDL_SCANCODE_RIGHT);

        if (left != right) {
            camera->x += (right ? 1.0f : -1.0f) * kFreeLookSpeed * dt;
            session.freeLookSeconds = kFreeLookHold;
        } else if (session.freeLookSeconds > 0.0f) {
            // Held where the player left it, so a glance at your own castle
            // isn't yanked away the instant you let go of the key.
            session.freeLookSeconds -= dt;
        } else {
            // Exponential catch-up: it closes a fixed FRACTION of the
            // remaining distance each second, so it starts quickly and eases
            // in rather than arriving at a hard stop. Written against dt so
            // the feel does not change with the frame rate.
            const float target = cameraTargetX(world);
            camera->x += (target - camera->x) *
                         std::min(1.0f, kCameraFollowRate * dt);
        }

        // Clamped last, and unconditionally, so free-look obeys the same
        // limits as following does.
        camera->x = std::min(std::max(camera->x, 0.0f), kCameraMaxX);
        camera->y = 0.0f;
    }

    // --- The spawn bar -----------------------------------------------------

    void buildSpawnBar(World& world) {
        const int shown = visibleButtonCount();
        buttonPlate_.assign(shown, kInvalidEntity);
        buttonFill_.assign(shown, kInvalidEntity);
        buttonName_.assign(shown, kInvalidEntity);
        buttonCost_.assign(shown, kInvalidEntity);

        for (int slot = 0; slot < shown; ++slot) {
            const int kind = kindForButton(slot);
            if (kind < 0) continue;
            const UnitKind& stats = unitKind(kind);
            const float left = buttonLeft(slot);

            // Three pieces per button: the plate, a fill that shrinks as the
            // cooldown runs, and the label. The fill is drawn over the plate
            // and under the text, which the layer sort already guarantees
            // because entity ids increase in creation order within a layer.
            buttonPlate_[slot] =
                makeScreenRect(world, left, kButtonY, kButtonWidth,
                               kButtonHeight, 34, 38, 48, kHudLayer);
            buttonFill_[slot] =
                makeScreenRect(world, left, kButtonY + kButtonHeight - 4.0f,
                               kButtonWidth, 4.0f, stats.leftR, stats.leftG,
                               stats.leftB, kHudLayer);

            // The number is the KEY, which is the slot. It was the roster row,
            // `kind + 1`, left over from before the loadout: identical for the
            // default loadout, which carries the first four rows in order, and
            // wrong for every other — an OGRE carried first read "6 OGRE" while
            // 1 sent it and 6 did nothing.
            const std::string label =
                std::to_string(slot + 1) + " " + stats.name;
            buttonName_[slot] = createText(
                world, label, static_cast<int>(left) + 8,
                static_cast<int>(kButtonY) + 8, 2, stats.leftR, stats.leftG,
                stats.leftB, kHudLayer);
            buttonCost_[slot] = createText(
                world, std::to_string(static_cast<int>(stats.cost)),
                static_cast<int>(left) + 8, static_cast<int>(kButtonY) + 26, 2,
                220, 200, 140, kHudLayer);

            world.getComponent<Text>(buttonName_[slot])->screenSpace = true;
            world.getComponent<Text>(buttonCost_[slot])->screenSpace = true;

            hud_.push_back(buttonPlate_[slot]);
            hud_.push_back(buttonFill_[slot]);
            hud_.push_back(buttonName_[slot]);
            hud_.push_back(buttonCost_[slot]);
        }
    }

    void refreshSpawnBar(World& world, const Session& session, int fielded) {
        for (int slot = 0; slot < visibleButtonCount(); ++slot) {
            const int kind = kindForButton(slot);
            if (kind < 0) continue;
            const UnitKind& stats = unitKind(kind);
            const bool affordable = session.gold >= stats.cost &&
                                    fielded < populationCapFor(session, true);

            if (Text* name = world.getComponent<Text>(buttonName_[slot])) {
                name->r = affordable ? stats.leftR : 95;
                name->g = affordable ? stats.leftG : 95;
                name->b = affordable ? stats.leftB : 110;
            }
            if (Text* cost = world.getComponent<Text>(buttonCost_[slot])) {
                cost->r = affordable ? 220 : 95;
                cost->g = affordable ? 200 : 95;
                cost->b = affordable ? 140 : 110;
            }
            if (Sprite* plate = world.getComponent<Sprite>(buttonPlate_[slot])) {
                plate->r = affordable ? 44 : 28;
                plate->g = affordable ? 50 : 32;
                plate->b = affordable ? 64 : 40;
            }

            // The fill is THIS kind's own cooldown draining left to right, so
            // the bar shows when you can send each unit rather than one
            // shared timer that told you nothing about which button to press.
            if (Sprite* fill = world.getComponent<Sprite>(buttonFill_[slot])) {
                const float total = std::max(kMinSpawnCooldown, stats.cooldown);
                const float left = kind < kMaxUnitKinds
                                       ? std::max(0.0f, session.spawnCooldowns[kind])
                                       : 0.0f;
                fill->width =
                    static_cast<int>(kButtonWidth * (1.0f - left / total));
            }
        }
    }

    // --- The hero button ---------------------------------------------------

    void buildHeroButton(World& world) {
        if (heroKindIndex() < 0) return;

        heroPlate_ = makeScreenRect(world, kHeroButtonX, kButtonY,
                                    kHeroButtonWidth, kButtonHeight,
                                    52, 46, 30, kHudLayer);
        heroText_ = createText(world, "", static_cast<int>(kHeroButtonX) + 8,
                               static_cast<int>(kButtonY) + 8, 2,
                               250, 235, 140, kHudLayer);
        world.getComponent<Text>(heroText_)->screenSpace = true;

        heroHint_ = createText(world, "H", static_cast<int>(kHeroButtonX) + 8,
                               static_cast<int>(kButtonY) + 26, 2,
                               170, 160, 120, kHudLayer);
        world.getComponent<Text>(heroHint_)->screenSpace = true;

        hud_.push_back(heroPlate_);
        hud_.push_back(heroText_);
        hud_.push_back(heroHint_);
    }

    // Three states, and they have to look different: never used, out there
    // fighting, and gone for good. Collapsing the last two into "unavailable"
    // would hide the only thing the player can do anything about.
    void refreshHeroButton(World& world, const Session& session) {
        if (heroText_ == kInvalidEntity) return;

        const char* label = "HERO READY";
        const char* hint = "H";
        unsigned char r = 250, g = 235, b = 140;
        unsigned char plateR = 62, plateG = 56, plateB = 34;

        if (session.heroFallen) {
            label = "HERO FALLEN";
            hint = "NOT THIS BATTLE";
            r = 130; g = 100; b = 100;
            plateR = 40; plateG = 28; plateB = 28;
        } else if (session.heroSummoned) {
            label = "HERO OUT";
            hint = "ON THE FIELD";
            r = 200; g = 190; b = 130;
            plateR = 52; plateG = 48; plateB = 32;
        }

        if (Text* text = world.getComponent<Text>(heroText_)) {
            text->value = label;
            text->r = r; text->g = g; text->b = b;
        }
        if (Text* text = world.getComponent<Text>(heroHint_)) {
            text->value = hint;
        }
        if (Sprite* plate = world.getComponent<Sprite>(heroPlate_)) {
            plate->r = plateR; plate->g = plateG; plate->b = plateB;
        }
    }

    // --- The spell panel ---------------------------------------------------

    void buildSpellPanel(World& world) {
        manaPlate_ = makeScreenRect(world, kSpellX, kManaBarY, kSpellWidth,
                                    kManaBarHeight, 30, 34, 50, kHudLayer);
        manaFill_ = makeScreenRect(world, kSpellX, kManaBarY, kSpellWidth,
                                   kManaBarHeight, 90, 130, 220, kHudLayer);
        // Labelled, and on top of the fill so it stays readable however full
        // the bar is. Without this it was an unnamed blue strip pressed
        // against the bottom of the upgrade panel, and read as a progress bar
        // belonging to SUPPLY — three spells priced in a resource the screen
        // never named.
        manaText_ = createText(world, "", static_cast<int>(kSpellX) + 8,
                               static_cast<int>(kManaBarY) + 1, 2,
                               235, 240, 255, kHudLayer);
        world.getComponent<Text>(manaText_)->screenSpace = true;
        hud_.push_back(manaPlate_);
        hud_.push_back(manaFill_);
        hud_.push_back(manaText_);

        for (int spell = 0; spell < kSpellCount; ++spell) {
            const float top = spellTop(spell);
            spellPlate_[spell] = makeScreenRect(world, kSpellX, top, kSpellWidth,
                                                kSpellHeight, 34, 38, 52,
                                                kHudLayer);
            spellText_[spell] =
                createText(world, "", static_cast<int>(kSpellX) + 8,
                           static_cast<int>(top) + 8, 2, 190, 200, 230,
                           kHudLayer);
            world.getComponent<Text>(spellText_[spell])->screenSpace = true;

            hud_.push_back(spellPlate_[spell]);
            hud_.push_back(spellText_[spell]);
        }
    }

    void refreshSpellPanel(World& world, const Session& session) {
        if (Sprite* fill = world.getComponent<Sprite>(manaFill_)) {
            fill->width =
                static_cast<int>(kSpellWidth * (session.mana / kMaxMana));
        }
        if (Text* text = world.getComponent<Text>(manaText_)) {
            text->value = "MANA " + std::to_string(static_cast<int>(session.mana));
        }

        for (int spell = 0; spell < kSpellCount; ++spell) {
            const SpellKind& kind = spellKind(spell);
            const bool affordable = session.mana >= kind.manaCost;
            const bool armed = session.armedSpell == spell;
            const bool active = spell == static_cast<int>(Spell::Rage) &&
                                session.rageSeconds > 0.0f;

            if (Text* text = world.getComponent<Text>(spellText_[spell])) {
                text->value = std::string(kind.hint) + " " + kind.name + "  " +
                              std::to_string(static_cast<int>(kind.manaCost));
                if (armed) text->value += "  AIM";
                if (active) text->value += "  ON";

                text->r = affordable ? (armed ? 255 : 190) : 95;
                text->g = affordable ? (armed ? 235 : 200) : 95;
                text->b = affordable ? (armed ? 150 : 230) : 110;
            }
            if (Sprite* plate = world.getComponent<Sprite>(spellPlate_[spell])) {
                plate->r = armed ? 70 : (affordable ? 44 : 28);
                plate->g = armed ? 62 : (affordable ? 50 : 32);
                plate->b = armed ? 40 : (affordable ? 68 : 40);
            }
        }
    }

    // --- The upgrade panel -------------------------------------------------

    void buildUpgradePanel(World& world) {
        for (int index = 0; index < kUpgradeCount; ++index) {
            const float top = upgradeTop(index);
            upgradePlate_[index] =
                makeScreenRect(world, kUpgradeX, top, kUpgradeWidth,
                               kUpgradeHeight, 34, 38, 48, kHudLayer);
            upgradeText_[index] =
                createText(world, "", static_cast<int>(kUpgradeX) + 8,
                           static_cast<int>(top) + 8, 2, 200, 200, 215,
                           kHudLayer);
            world.getComponent<Text>(upgradeText_[index])->screenSpace = true;

            hud_.push_back(upgradePlate_[index]);
            hud_.push_back(upgradeText_[index]);
        }
    }

    void refreshUpgradePanel(World& world, const Session& session) {
        for (int index = 0; index < kUpgradeCount; ++index) {
            const int owned = session.upgrades[index];
            const float cost = upgradeCost(index, owned);
            const bool affordable = session.gold >= cost;

            if (Text* text = world.getComponent<Text>(upgradeText_[index])) {
                text->value = std::string(upgradeKind(index).name) + " " +
                              std::to_string(owned) + "  " +
                              std::to_string(static_cast<int>(cost));
                text->r = affordable ? 210 : 95;
                text->g = affordable ? 210 : 95;
                text->b = affordable ? 225 : 110;
            }
            if (Sprite* plate = world.getComponent<Sprite>(upgradePlate_[index])) {
                plate->r = affordable ? 44 : 28;
                plate->g = affordable ? 50 : 32;
                plate->b = affordable ? 64 : 40;
            }
        }
    }

    // --- The minimap -------------------------------------------------------

    void buildMinimap(World& world) {
        hud_.push_back(makeScreenRect(world, kMinimapX, kMinimapY,
                                      kMinimapWidth, kMinimapHeight, 30, 34, 44,
                                      kHudLayer));

        // Castles first so the front-line markers draw over them: same layer,
        // and within a layer the draw order is by entity id.
        minimapLeftCastle_ = makeScreenRect(world, 0.0f, kMinimapY, 4.0f,
                                            kMinimapHeight, 90, 150, 200,
                                            kHudLayer);
        minimapRightCastle_ = makeScreenRect(world, 0.0f, kMinimapY, 4.0f,
                                             kMinimapHeight, 200, 110, 100,
                                             kHudLayer);
        minimapLeftFront_ = makeScreenRect(world, 0.0f, kMinimapY - 3.0f,
                                           kMinimapMarkerWidth,
                                           kMinimapHeight + 6.0f, 140, 210, 250,
                                           kHudLayer);
        minimapRightFront_ = makeScreenRect(world, 0.0f, kMinimapY - 3.0f,
                                            kMinimapMarkerWidth,
                                            kMinimapHeight + 6.0f, 250, 150, 130,
                                            kHudLayer);

        hud_.push_back(minimapLeftCastle_);
        hud_.push_back(minimapRightCastle_);
        hud_.push_back(minimapLeftFront_);
        hud_.push_back(minimapRightFront_);
    }

    Entity makeScreenRect(World& world, float x, float y, float width,
                          float height, unsigned char r, unsigned char g,
                          unsigned char b, int layer) {
        const Entity entity =
            createRect(world, x, y, width, height, r, g, b, layer);
        world.getComponent<Sprite>(entity)->screenSpace = true;
        return entity;
    }

    void refreshMinimap(World& world) {
        placeMarker(world, minimapLeftCastle_, kLeftCastleX + kCastleWidth / 2.0f,
                    4.0f);
        placeMarker(world, minimapRightCastle_,
                    kRightCastleX + kCastleWidth / 2.0f, 4.0f);
        placeMarker(world, minimapLeftFront_, frontLineX(world, true),
                    kMinimapMarkerWidth);
        placeMarker(world, minimapRightFront_, frontLineX(world, false),
                    kMinimapMarkerWidth);
    }

    // World x -> a position along the strip, with the marker centred on it and
    // kept inside the strip at both ends.
    void placeMarker(World& world, Entity marker, float worldX, float width) {
        Transform* transform = world.getComponent<Transform>(marker);
        if (!transform) return;

        const float fraction = std::min(std::max(worldX / kWorldWidth, 0.0f), 1.0f);
        transform->x = kMinimapX + fraction * (kMinimapWidth - width);
    }

    void refreshHud(World& world, const Session& session) {
        const int gold = static_cast<int>(session.gold);
        if (gold != shownGold_) {
            if (Text* text = world.getComponent<Text>(goldText_)) {
                text->value = "GOLD " + std::to_string(gold);
            }
            shownGold_ = gold;
        }

        const int fielded = countUnits(world, true);
        if (fielded != shownPopulation_) {
            if (Text* text = world.getComponent<Text>(popText_)) {
                text->value = "UNITS " + std::to_string(fielded) + "/" +
                              std::to_string(kPopulationCap);
            }
            shownPopulation_ = fielded;
        }

        // Dim whatever you cannot currently buy, so affordability is readable
        // without doing arithmetic against the gold counter.
        refreshSpawnBar(world, session, fielded);
        refreshUpgradePanel(world, session);
        refreshHeroButton(world, session);
        refreshSpellPanel(world, session);

        if (Text* text = world.getComponent<Text>(cannonText_)) {
            const bool ready = session.cannonCooldown <= 0.0f &&
                              session.gold >= kCannonCost;
            // The price is only mentioned when there is one. A free weapon
            // labelled "0G" reads as broken, and "CANNON 4" on its own never
            // said 4 of what.
            const std::string price =
                kCannonCost > 0.0f
                    ? " - " + std::to_string(static_cast<int>(kCannonCost)) + "G"
                    : "";
            text->value =
                ready ? "CANNON READY - CLICK THE FIELD" + price
                      : "CANNON RELOADING - " +
                            std::to_string(
                                static_cast<int>(session.cannonCooldown) + 1) + "S";
            text->r = ready ? 220 : 110;
            text->g = ready ? 200 : 110;
            text->b = ready ? 140 : 125;
        }

        updateHealthText(world, leftHealthText_, "YOU  ", true);
        updateHealthText(world, rightHealthText_, "ENEMY ", false);
        refreshClock(world, session);
    }

    // Rewritten only when the second it shows changes, like the gold.
    //
    // Three states, and the words change with them so the rule is on the
    // screen rather than in a manual: TIME 9:59 while there is time, SWARM IN
    // 0:59 in red for the last minute, then SWARM +0:12 counting the swarm up.
    void refreshClock(World& world, const Session& session) {
        const bool swarming = session.swarming;
        const float left = std::max(0.0f, kBattleSeconds - session.elapsed);
        const float into = std::max(0.0f, session.elapsed - kBattleSeconds);

        // The banner, for the first few seconds of it.
        if (Text* banner = world.getComponent<Text>(swarmBanner_)) {
            const std::string wanted =
                swarming && into < kSwarmBannerSeconds ? "THE SWARM IS HERE" : "";
            if (banner->value != wanted) {
                banner->value = wanted;
                if (Transform* at = world.getComponent<Transform>(swarmBanner_)) {
                    at->x = (static_cast<float>(kWindowWidth) -
                             static_cast<float>(textWidth(wanted, banner->scale))) / 2.0f;
                }
            }
        }

        // Negative while counting the swarm UP, so the two runs of seconds can
        // never be mistaken for each other.
        const int second = swarming ? -1 - static_cast<int>(std::floor(into))
                                    : static_cast<int>(std::ceil(left));
        if (second == shownClock_) return;
        shownClock_ = second;

        Text* text = world.getComponent<Text>(clockText_);
        Transform* at = world.getComponent<Transform>(clockText_);
        if (!text || !at) return;

        const bool warning = swarming || left <= kClockWarningSeconds;
        if (swarming) {
            text->value = "SWARM +" + clockText(std::floor(into));
        } else if (warning) {
            text->value = "SWARM IN " + clockText(left);
        } else {
            text->value = "TIME " + clockText(left);
        }
        text->r = warning ? 240 : 140;
        text->g = warning ? 120 : 150;
        text->b = warning ? 100 : 170;
        at->x = kMinimapX + kMinimapWidth / 2.0f -
                static_cast<float>(textWidth(text->value, text->scale)) / 2.0f;
    }

    void updateHealthText(World& world, Entity textEntity,
                          const std::string& label, bool leftSide) {
        const Entity castle = findCastle(world, leftSide);
        Text* text = world.getComponent<Text>(textEntity);
        if (castle == kInvalidEntity || !text) return;

        const Castle* health = world.getComponent<Castle>(castle);
        if (!health) return;

        text->value = label + std::to_string(
                                  static_cast<int>(std::max(0.0f, health->health)));
    }

    int stage_ = 0;
    bool returnToCampaign_ = false;

    // The permanent upgrades, read once when the battle starts. A snapshot,
    // so buying something between battles cannot change a fight in progress.
    int perks_[kPerkCount] = {};
    float damageScale_ = 1.0f;
    float heroDamageScale_ = 1.0f;
    int heroPath_ = 0;
    float heroPathDamage_ = 1.0f;
    float heroHealthScale_ = 1.0f;
    float heroDelayScale_ = 1.0f;
    float heroHealPerSecond_ = 0.0f;
    bool heroHitsAir_ = false;
    Entity sessionEntity_ = kInvalidEntity;
    Entity cameraEntity_ = kInvalidEntity;
    Entity goldText_ = kInvalidEntity;
    Entity popText_ = kInvalidEntity;
    // Sized at runtime: the roster's length is whatever the data file made it.
    std::vector<Entity> buttonPlate_;
    std::vector<Entity> buttonFill_;
    std::vector<Entity> buttonName_;
    std::vector<Entity> buttonCost_;
    Entity leftHealthText_ = kInvalidEntity;
    Entity rightHealthText_ = kInvalidEntity;
    Entity cannonText_ = kInvalidEntity;
    Entity manaPlate_ = kInvalidEntity;
    Entity manaFill_ = kInvalidEntity;
    Entity manaText_ = kInvalidEntity;
    Entity spellPlate_[kSpellCount] = {};
    Entity spellText_[kSpellCount] = {};
    Entity heroPlate_ = kInvalidEntity;
    Entity heroText_ = kInvalidEntity;
    Entity heroHint_ = kInvalidEntity;
    Entity upgradePlate_[kUpgradeCount] = {};
    Entity upgradeText_[kUpgradeCount] = {};
    Entity minimapLeftCastle_ = kInvalidEntity;
    Entity minimapRightCastle_ = kInvalidEntity;
    Entity minimapLeftFront_ = kInvalidEntity;
    Entity minimapRightFront_ = kInvalidEntity;
    Entity clockText_ = kInvalidEntity;
    Entity swarmBanner_ = kInvalidEntity;
    std::vector<Entity> field_;
    std::vector<Entity> hud_;
    int shownGold_ = -1;
    int shownPopulation_ = -1;
    int shownClock_ = -1;
    bool overlayShown_ = false;
};

// The campaign, as a list you pick from.
//
// Locked stages are drawn but not clickable, which is the point of showing
// them: a list of eight with two lit says "there is more of this" in a way a
// single "next battle" button never could.
class StageSelectScene : public Scene {
public:
    void onEnter(World& world) override {
        const Campaign& campaign = campaignOf(world);

        owned_.push_back(createCenteredText(world, "CHOOSE A BATTLE", 60, 4,
                                            220, 200, 140, kHudLayer));

        for (int stage = 0; stage < visibleStageCount(); ++stage) {
            const bool unlocked = stage < campaign.stagesUnlocked;
            const float top = stageTop(stage);

            Entity plate = createRect(world, kStageX, top, kStageWidth,
                                      kStageHeight,
                                      unlocked ? 44 : 26,
                                      unlocked ? 50 : 30,
                                      unlocked ? 64 : 38, kHudLayer);
            world.getComponent<Sprite>(plate)->screenSpace = true;
            owned_.push_back(plate);

            const std::string label =
                std::to_string(stage + 1) + "  " +
                (unlocked ? stageKind(stage).name : "- LOCKED -");
            owned_.push_back(createText(
                world, label, static_cast<int>(kStageX) + 12,
                static_cast<int>(top) + 10, 2,
                unlocked ? 215 : 90, unlocked ? 215 : 90, unlocked ? 230 : 105,
                kHudLayer));
        }

        buildArmoury(world, campaign);

        owned_.push_back(createCenteredText(
            world, "CLICK A BATTLE - ENTER FOR THE LATEST - A ARMY - H HERO", 470, 2,
            160, 160, 185, kHudLayer));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 496, 2,
                                            140, 140, 165, kHudLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void onResume(World& world) override {
        // Coming back from a battle: a stage may have just been unlocked and
        // the bank has almost certainly changed, so the whole screen is
        // rebuilt rather than left showing what it showed before.
        onExit(world);
        world.flushDestroyed();
        onEnter(world);
    }

    // The armoury: what the campaign's winnings buy.
    void buildArmoury(World& world, const Campaign& campaign) {
        owned_.push_back(createText(
            world, "GOLD " + std::to_string(campaign.bank),
            static_cast<int>(kPerkX), static_cast<int>(kPerkY) - 34, 3,
            235, 220, 150, kHudLayer));

        for (int perk = 0; perk < kPerkCount; ++perk) {
            const PerkKind& kind = perkKind(perk);
            const float top = perkTop(perk);
            const float cost = perkCost(perk, campaign.perks[perk]);
            const bool affordable = static_cast<float>(campaign.bank) >= cost;

            Entity plate = createRect(world, kPerkX, top, kPerkWidth,
                                      kPerkHeight,
                                      affordable ? 44 : 30,
                                      affordable ? 50 : 34,
                                      affordable ? 64 : 42, kHudLayer);
            world.getComponent<Sprite>(plate)->screenSpace = true;
            owned_.push_back(plate);

            owned_.push_back(createText(
                world,
                std::string(kind.name) + " " + std::to_string(campaign.perks[perk]),
                static_cast<int>(kPerkX) + 8, static_cast<int>(top) + 6, 2,
                affordable ? 215 : 100, affordable ? 215 : 100,
                affordable ? 230 : 115, kHudLayer));

            // The cost is right-aligned on the NAME line rather than appended
            // to the effect line.
            //
            // "+40 START GOLD  160" is nineteen characters, which at this
            // scale is 226 pixels inside a 220-pixel plate: TREASURY's price
            // hung out over the gap towards the stage list, and one more digit
            // would have put it under the list itself. Splitting the line
            // fixes it for every row and keeps working when a price reaches
            // four figures, which the geometric cost curve reaches quickly.
            const std::string price = std::to_string(static_cast<int>(cost));
            owned_.push_back(createText(
                world, price,
                static_cast<int>(kPerkX + kPerkWidth) - 8 - textWidth(price, 2),
                static_cast<int>(top) + 6, 2,
                affordable ? 235 : 100, affordable ? 220 : 100,
                affordable ? 150 : 115, kHudLayer));

            owned_.push_back(createText(
                world, kind.effectText,
                static_cast<int>(kPerkX) + 8, static_cast<int>(top) + 24, 2,
                affordable ? 190 : 90, affordable ? 175 : 90,
                affordable ? 120 : 105, kHudLayer));
        }
    }

    // Buys one level of a permanent upgrade, and saves immediately — the whole
    // point of the bank is that it survives the window closing.
    void buyPerk(World& world, Campaign& campaign, int perk) {
        if (perk < 0 || perk >= kPerkCount) return;

        const float cost = perkCost(perk, campaign.perks[perk]);
        if (static_cast<float>(campaign.bank) < cost) return;

        campaign.bank -= static_cast<int>(cost);
        ++campaign.perks[perk];
        saveCampaign(campaign);

        onExit(world);
        world.flushDestroyed();
        onEnter(world);
    }

    // Starts a battle, taking this screen down first.
    //
    // `push` does NOT call onExit on the scene underneath — only pop and
    // replace do — and rendering is driven by the components in the World
    // rather than by which scene put them there. So pushing the battle left
    // the entire stage list and armoury sitting in the world, drawn straight
    // over the top of the fight: "CHOOSE A BATTLE" across the middle of the
    // battlefield, eight stage rows through the HUD, the armoury over the
    // spawn bar.
    //
    // Push is still right. The battle has to return HERE when it ends, which
    // is what pop and onResume do; replace would throw this screen away and
    // there would be nothing to come back to. What was missing is that a
    // full-screen scene has to clear its own picture on the way out, and only
    // it knows what it drew. onResume already rebuilds everything from
    // scratch, so there is nothing to preserve.
    void startBattle(World& world, SceneStack& scenes) {
        onExit(world);
        world.flushDestroyed();
        scenes.push(makePlayScene());
    }

    void update(World& world, InputManager& input, float,
                SceneStack& scenes) override {
        Campaign& campaign = campaignOf(world);

        if (input.wasKeyPressed(SDL_SCANCODE_Q)) {
            scenes.pop();
            return;
        }

        // The hero screen. Pushed, not replaced, so it comes back here — and
        // this screen takes its own picture down first, for the reason
        // startBattle spells out: push does not call onExit underneath, and
        // the renderer draws components rather than scenes.
        if (input.wasKeyPressed(SDL_SCANCODE_H)) {
            onExit(world);
            world.flushDestroyed();
            scenes.push(makeHeroScene());
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_A)) {
            onExit(world);
            world.flushDestroyed();
            scenes.push(makeArmyScene());
            return;
        }

        // Enter plays the furthest stage reached, which is what you want nine
        // times in ten and saves aiming at a row.
        if (input.wasKeyPressed(SDL_SCANCODE_RETURN) ||
            input.wasKeyPressed(SDL_SCANCODE_KP_ENTER) ||
            input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            campaign.currentStage = campaign.stagesUnlocked - 1;
            startBattle(world, scenes);
            return;
        }

        if (!input.wasMousePressed()) return;

        const float mouseX = static_cast<float>(input.mouseX());
        const float mouseY = static_cast<float>(input.mouseY());

        const int perk = perkAt(mouseX, mouseY);
        if (perk >= 0) {
            buyPerk(world, campaign, perk);
            return;
        }

        const int picked = stageAt(mouseX, mouseY);
        if (picked < 0 || picked >= campaign.stagesUnlocked) return;

        campaign.currentStage = picked;
        startBattle(world, scenes);
    }

    bool simulatesWorld() const override { return false; }

private:
    std::vector<Entity> owned_;
};

// The army screen: what you own, what you carry, and what you have trained.
class ArmyScene : public Scene {
public:
    // This screen prints "Q OR ESC TO GO BACK" and means it. Without this the
    // engine took Escape first and closed the window instead.
    bool escapeQuits() const override { return false; }

    void onEnter(World& world) override {
        Campaign& campaign = campaignOf(world);
        seedLoadout(campaign);

        owned_.push_back(createCenteredText(world, "YOUR ARMY", 44, 4,
                                            235, 220, 150, kHudLayer));
        owned_.push_back(createText(
            world, "GOLD " + std::to_string(campaign.bank), 20, 18, 3,
            235, 220, 150, kHudLayer));

        owned_.push_back(createCenteredText(
            world,
            "CARRYING " + std::to_string(countCarried(campaign)) + " OF " +
                std::to_string(kLoadoutSlots) + " - CLICK A NAME TO CARRY OR DROP IT",
            88, 2, 170, 170, 195, kHudLayer));

        const int rows = std::min(sellableKindCount(), kMaxVisibleArmyRows);
        for (int index = 0; index < rows; ++index) {
            const int kind = sellableKind(index);
            if (kind < 0) continue;
            buildRow(world, campaign, index, kind);
        }

        // Above the hint rather than in place of it, so the way out never
        // disappears behind a message about something else — and far enough
        // above that the two do not read as one paragraph. At 10 pixels they
        // did: a screenshot showed the warning and GO BACK stacked tight
        // enough to look like a single two-line block of instructions.
        if (noticeSeconds_ > 0.0f && !notice_.empty()) {
            owned_.push_back(createCenteredText(
                world, notice_, static_cast<int>(kArmyHintY) - 26, 2,
                235, 170, 110, kHudLayer));
        }

        owned_.push_back(createCenteredText(
            world, "Q OR ESC TO GO BACK", static_cast<int>(kArmyHintY) + 6, 2,
            160, 160, 185, kHudLayer));
    }

    void buildRow(World& world, const Campaign& campaign, int index, int kind) {
        const UnitKind& unit = unitKind(kind);
        const float top = armyTop(index);
        const bool carried = isCarried(campaign, kind);
        const int level = campaign.unitLevels[kind];
        const bool maxed = level >= kMaxUnitLevel;
        const float cost = trainCost(kind, level);
        const bool affordable = !maxed && static_cast<float>(campaign.bank) >= cost;

        Entity plate = createRect(world, kArmyX, top, kArmyWidth, kArmyHeight,
                                  carried ? 50 : 30, carried ? 58 : 34,
                                  carried ? 74 : 42, kHudLayer);
        world.getComponent<Sprite>(plate)->screenSpace = true;
        owned_.push_back(plate);

        // A swatch in the unit's own colour, so the row and the thing it puts
        // on the field are recognisably the same unit.
        Entity swatch = createRect(world, kArmyX + 10.0f, top + 10.0f, 20.0f,
                                   20.0f, unit.leftR, unit.leftG, unit.leftB,
                                   kHudLayer);
        world.getComponent<Sprite>(swatch)->screenSpace = true;
        owned_.push_back(swatch);

        owned_.push_back(createText(
            world, carried ? "CARRIED" : "-",
            static_cast<int>(kArmyX) + 40, static_cast<int>(top) + 4, 1,
            carried ? 190 : 110, carried ? 230 : 110,
            carried ? 200 : 125, kHudLayer));

        owned_.push_back(createText(
            world, unit.name, static_cast<int>(kArmyX) + 40,
            static_cast<int>(top) + 16, 2,
            carried ? 235 : 150, carried ? 235 : 150,
            carried ? 245 : 165, kHudLayer));

        // The numbers that decide whether it is worth a slot, including what
        // training has already done to them — the point of a level is visible
        // here or nowhere.
        const float health = unit.health * trainedHealth(kind, level);
        const float damage = unit.damage * trainedDamage(kind, level);
        owned_.push_back(createText(
            world,
            std::to_string(static_cast<int>(unit.cost)) + "G   HP " +
                std::to_string(static_cast<int>(health)) + "   DMG " +
                std::to_string(static_cast<int>(damage)) + "   RANGE " +
                std::to_string(static_cast<int>(unit.range)) +
                (unit.flying ? "   FLIES" : "") +
                (unit.hitsAir ? "   HITS AIR" : ""),
            static_cast<int>(kArmyX) + 190, static_cast<int>(top) + 8, 1,
            carried ? 200 : 130, carried ? 205 : 130,
            carried ? 220 : 145, kHudLayer));

        owned_.push_back(createText(
            world, "LEVEL " + std::to_string(level) + "/" +
                       std::to_string(kMaxUnitLevel),
            static_cast<int>(kArmyX) + 190, static_cast<int>(top) + 22, 1,
            carried ? 190 : 125, carried ? 175 : 125,
            carried ? 120 : 135, kHudLayer));

        Entity train = createRect(world, kTrainX, top + 4.0f,
                                  kTrainWidth - 8.0f, kArmyHeight - 8.0f,
                                  affordable ? 58 : 34, affordable ? 66 : 38,
                                  affordable ? 82 : 46, kHudLayer);
        world.getComponent<Sprite>(train)->screenSpace = true;
        owned_.push_back(train);

        const std::string label =
            maxed ? "MAX" : ("TRAIN  " + std::to_string(static_cast<int>(cost)));
        owned_.push_back(createText(
            world, label,
            static_cast<int>(kTrainX) + 10, static_cast<int>(top) + 14, 2,
            maxed ? 150 : (affordable ? 235 : 115),
            maxed ? 200 : (affordable ? 220 : 115),
            maxed ? 160 : (affordable ? 150 : 130), kHudLayer));
    }

    static bool isCarried(const Campaign& campaign, int kind) {
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            if (campaign.loadout[slot] == kind) return true;
        }
        return false;
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void rebuild(World& world) {
        onExit(world);
        world.flushDestroyed();
        onEnter(world);
    }

    // Writes down the loadout a player already had, the first time they come
    // to look at it.
    //
    // A new campaign carries nothing, and "nothing chosen" is what makes the
    // spawn bar fall back to the first few roster rows — so the player has
    // been fighting with four units that were never recorded anywhere. Opening
    // this screen and clicking one of them used to mean "carry exactly this
    // one", silently dropping the other three: the first thing the screen ever
    // did was take three units away for touching it.
    //
    // Seeding makes the fallback explicit before it can be edited, so the
    // first click is a toggle rather than a reset. It writes down what was
    // already true, which is why it is allowed to save.
    void seedLoadout(Campaign& campaign) {
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            if (campaign.loadout[slot] >= 0) return;  // already chosen
        }
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            campaign.loadout[slot] = sellableKind(slot);
        }
        saveCampaign(campaign);
    }

    static int countCarried(const Campaign& campaign) {
        int count = 0;
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            if (campaign.loadout[slot] >= 0) ++count;
        }
        return count;
    }

    // Says why nothing happened, for a couple of seconds.
    //
    // Both of this screen's refusals used to be silent — dropping your last
    // unit, and carrying one more than there are slots for. A click that does
    // nothing and explains nothing is indistinguishable from a click the game
    // missed, and the player's next move is to click it again harder.
    void warn(World& world, const char* text) {
        notice_ = text;
        noticeSeconds_ = kNoticeSeconds;
        rebuild(world);
    }

    // Carry it, or put it back. Dropping leaves a hole rather than shuffling
    // the rest along: the slot a unit sits in is the key that sends it, and
    // re-ordering the bar under a player who has learned it is a worse cost
    // than an empty button. The save keeps the holes too, which it did not
    // always do.
    void toggleCarried(World& world, Campaign& campaign, int kind) {
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            if (campaign.loadout[slot] == kind) {
                // Never all the way to nothing. An empty loadout is not a
                // strategy, it is a player who cannot spawn anything and has
                // no way to find out why — and it is indistinguishable, in a
                // save file, from a campaign that never chose at all, which
                // would hand the defaults back on the next launch as if the
                // choice had not been made.
                if (countCarried(campaign) <= 1) {
                    warn(world, "CARRY AT LEAST ONE UNIT");
                    return;
                }
                campaign.loadout[slot] = -1;
                saveCampaign(campaign);
                rebuild(world);
                return;
            }
        }
        for (int slot = 0; slot < kLoadoutSlots; ++slot) {
            if (campaign.loadout[slot] < 0) {
                campaign.loadout[slot] = kind;
                saveCampaign(campaign);
                rebuild(world);
                return;
            }
        }
        // Nothing free to put it in. Silently swapping something out would
        // make a mis-click cost a unit the player had chosen deliberately, so
        // the refusal stands — but it now says so.
        warn(world, "NO FREE SLOT - DROP ONE FIRST");
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        Campaign& campaign = campaignOf(world);

        // A notice is on screen for a couple of seconds and then is not. The
        // rebuild is what takes it down, so it only happens on the frame the
        // countdown actually runs out.
        if (noticeSeconds_ > 0.0f) {
            noticeSeconds_ -= dt;
            if (noticeSeconds_ <= 0.0f) {
                noticeSeconds_ = 0.0f;
                notice_.clear();
                rebuild(world);
            }
        }

        if (input.wasKeyPressed(SDL_SCANCODE_Q) ||
            input.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
            scenes.pop();
            return;
        }

        if (!input.wasMousePressed()) return;

        const float mouseX = static_cast<float>(input.mouseX());
        const float mouseY = static_cast<float>(input.mouseY());

        const int row = armyRowAt(mouseX, mouseY);
        if (row < 0) return;

        const int kind = sellableKind(row);
        if (kind < 0) return;

        if (!armyTrainHit(mouseX, mouseY)) {
            toggleCarried(world, campaign, kind);
            return;
        }

        const int level = campaign.unitLevels[kind];
        if (level >= kMaxUnitLevel) {
            warn(world, "ALREADY AT THE HIGHEST LEVEL");
            return;
        }

        const float cost = trainCost(kind, level);
        if (static_cast<float>(campaign.bank) < cost) {
            warn(world, "NOT ENOUGH GOLD TO TRAIN THAT");
            return;
        }

        campaign.bank -= static_cast<int>(cost);
        ++campaign.unitLevels[kind];
        saveCampaign(campaign);
        rebuild(world);
    }

    bool simulatesWorld() const override { return false; }

private:
    static constexpr float kNoticeSeconds = 2.2f;

    std::vector<Entity> owned_;
    std::string notice_;
    float noticeSeconds_ = 0.0f;
};

// The hero screen: choose a path once, then spend the bank inside it.
class HeroScene : public Scene {
public:
    bool escapeQuits() const override { return false; }  // same as ArmyScene

    void onEnter(World& world) override {
        const Campaign& campaign = campaignOf(world);
        const int chosen = campaign.heroPath;

        owned_.push_back(createCenteredText(world, "YOUR HERO", 56, 5,
                                            235, 220, 150, kHudLayer));
        owned_.push_back(createText(
            world, "GOLD " + std::to_string(campaign.bank), 20, 20, 3,
            235, 220, 150, kHudLayer));

        owned_.push_back(createCenteredText(
            world,
            chosen == 0 ? "CHOOSE A PATH - IT CANNOT BE CHANGED"
                        : "PATH CHOSEN - SPEND THE BANK INSIDE IT",
            110, 2, 170, 170, 195, kHudLayer));

        for (int path = 1; path < kHeroPathCount; ++path) {
            const HeroPathKind& kind = heroPath(path);
            const float left = pathLeft(path - 1);

            // Three states, and they have to look different: the one you took,
            // the ones you can still take, and the ones you can no longer take
            // because you took another. A locked-out path is drawn dimmest —
            // it is not a thing you failed to afford, it is a road you did not
            // walk, and it should read as closed rather than as expensive.
            const bool isChosen = path == chosen;
            const bool available = chosen == 0;

            Entity plate = createRect(
                world, left, kPathY, kPathWidth, kPathHeight,
                isChosen ? 54 : (available ? 40 : 26),
                isChosen ? 62 : (available ? 46 : 28),
                isChosen ? 78 : (available ? 58 : 34), kHudLayer);
            world.getComponent<Sprite>(plate)->screenSpace = true;
            owned_.push_back(plate);

            const unsigned char bright =
                isChosen ? 240 : (available ? 210 : 92);
            owned_.push_back(createText(
                world, kind.name, static_cast<int>(left) + 12,
                static_cast<int>(kPathY) + 10, 3, bright, bright,
                static_cast<unsigned char>(isChosen ? 170 : bright), kHudLayer));

            owned_.push_back(createText(
                world, kind.blurb, static_cast<int>(left) + 12,
                static_cast<int>(kPathY) + 40, 1,
                isChosen ? 200 : (available ? 170 : 80),
                isChosen ? 200 : (available ? 170 : 80),
                isChosen ? 215 : (available ? 195 : 92), kHudLayer));

            owned_.push_back(createText(
                world,
                kind.hitsAir ? "REACHES THE SKY" : "GROUND ONLY",
                static_cast<int>(left) + 12, static_cast<int>(kPathY) + 58, 2,
                kind.hitsAir ? 200 : 150, kind.hitsAir ? 180 : 150,
                kind.hitsAir ? 240 : 165, kHudLayer));

            // What the path costs you, on the plate you choose it from.
            //
            // The choice is permanent, and without this the screen asked for
            // it while showing only prose: "a wall that walks" does not tell
            // anybody that a WARDEN carries twice a FALCONER's health and hits
            // for two thirds as much. A trade you cannot see is not a trade
            // you can make.
            //
            // Percentages of the roster's hero rather than raw numbers,
            // because the roster row is the one thing all three share and the
            // interesting fact is the RATIO between them.
            auto percent = [](float value) {
                return std::to_string(static_cast<int>(value * 100.0f + 0.5f)) + "%";
            };
            // The heal belongs on this line too, or the chaplain reads as a
            // strictly worse warden: 110% health and 60% damage against 145%
            // and 85%, with the one thing it is actually for left off the
            // plate. A path whose advantage is invisible is a path nobody
            // picks, and it would have looked like a balance problem rather
            // than a missing label.
            std::string line =
                "HP " + percent(kind.health) + "   DMG " + percent(kind.damage) +
                "   RATE " + percent(1.0f / kind.attackDelay);
            if (kind.healPerSecond > 0.0f) {
                line += "   MENDS " +
                        std::to_string(static_cast<int>(kind.healPerSecond)) + "/S";
            }
            owned_.push_back(createText(
                world, line,
                static_cast<int>(left) + 12, static_cast<int>(kPathY) + 76, 1,
                isChosen ? 215 : (available ? 185 : 85),
                isChosen ? 225 : (available ? 195 : 85),
                isChosen ? 200 : (available ? 175 : 95), kHudLayer));
        }

        if (chosen != 0) buildUpgrades(world, campaign);

        owned_.push_back(createCenteredText(
            world, "Q OR ESC TO GO BACK", 470, 2, 160, 160, 185, kHudLayer));
    }

    void buildUpgrades(World& world, const Campaign& campaign) {
        const int path = campaign.heroPath;
        const HeroPathKind& kind = heroPath(path);

        for (int up = 0; up < kHeroUpgradesPerPath; ++up) {
            const HeroUpgradeKind& row = kind.upgrades[up];
            const int owned = campaign.heroUpgrades[path][up];
            const bool maxed = owned >= row.maxLevel;
            const float cost = heroUpgradeCost(path, up, owned);
            const bool affordable =
                !maxed && static_cast<float>(campaign.bank) >= cost;

            const float top = heroUpgradeTop(up);
            Entity plate = createRect(world, kHeroUpgradeX, top,
                                      kHeroUpgradeWidth, kHeroUpgradeHeight,
                                      affordable ? 44 : 30,
                                      affordable ? 50 : 34,
                                      affordable ? 64 : 42, kHudLayer);
            world.getComponent<Sprite>(plate)->screenSpace = true;
            owned_.push_back(plate);

            owned_.push_back(createText(
                world,
                std::string(row.name) + "  " + std::to_string(owned) + "/" +
                    std::to_string(row.maxLevel),
                static_cast<int>(kHeroUpgradeX) + 10,
                static_cast<int>(top) + 6, 2,
                affordable ? 215 : 110, affordable ? 215 : 110,
                affordable ? 230 : 125, kHudLayer));

            owned_.push_back(createText(
                world, row.effectText, static_cast<int>(kHeroUpgradeX) + 10,
                static_cast<int>(top) + 25, 2,
                affordable ? 190 : 95, affordable ? 175 : 95,
                affordable ? 120 : 105, kHudLayer));

            // Right-aligned, the same lesson the armoury learned: a cost
            // appended to a label is a cost that eventually runs off the plate.
            const std::string right = maxed ? "MAX" : std::to_string(
                                                  static_cast<int>(cost));
            owned_.push_back(createText(
                world, right,
                static_cast<int>(kHeroUpgradeX + kHeroUpgradeWidth) - 10 -
                    textWidth(right, 2),
                static_cast<int>(top) + 15, 2,
                maxed ? 150 : (affordable ? 235 : 110),
                maxed ? 200 : (affordable ? 220 : 110),
                maxed ? 160 : (affordable ? 150 : 125), kHudLayer));
        }
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void rebuild(World& world) {
        onExit(world);
        world.flushDestroyed();
        onEnter(world);
    }

    void update(World& world, InputManager& input, float,
                SceneStack& scenes) override {
        Campaign& campaign = campaignOf(world);

        if (input.wasKeyPressed(SDL_SCANCODE_Q) ||
            input.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
            scenes.pop();
            return;
        }

        if (!input.wasMousePressed()) return;

        const float mouseX = static_cast<float>(input.mouseX());
        const float mouseY = static_cast<float>(input.mouseY());

        // Choosing is free and once. Nothing is spent, so there is no
        // affordability check — the cost of a path is the other two.
        if (campaign.heroPath == 0) {
            const int picked = pathAt(mouseX, mouseY);
            if (picked > 0) {
                campaign.heroPath = picked;
                saveCampaign(campaign);
                rebuild(world);
            }
            return;
        }

        const int upgrade = heroUpgradeAt(mouseX, mouseY);
        if (upgrade < 0 || upgrade >= kHeroUpgradesPerPath) return;

        // Checked here rather than trusted. `heroPath()` on the next line
        // clamps a bad path to a real one, so the ROW it returns is always
        // safe — but the array indexed below does no such thing, and the two
        // sitting side by side read as if they were equally protected. GCC
        // noticed the difference and MSVC did not: "array subscript 4 is above
        // array bounds of int[4][3]". Nothing currently writes an out-of-range
        // path, which is exactly the kind of guarantee that holds until a save
        // file, a test or a refactor says otherwise.
        const int path = campaign.heroPath;
        if (path <= 0 || path >= kHeroPathCount) return;

        const HeroUpgradeKind& row = heroPath(path).upgrades[upgrade];
        const int owned = campaign.heroUpgrades[path][upgrade];
        if (owned >= row.maxLevel) return;

        const float cost = heroUpgradeCost(path, upgrade, owned);
        if (static_cast<float>(campaign.bank) < cost) return;

        campaign.bank -= static_cast<int>(cost);
        ++campaign.heroUpgrades[path][upgrade];
        saveCampaign(campaign);
        rebuild(world);
    }

    bool simulatesWorld() const override { return false; }

private:
    std::vector<Entity> owned_;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createCenteredText(world, "LANE BATTLE", 92, 8,
                                            220, 200, 140, kHudLayer));

        // The roster line is BUILT from the roster rather than typed out.
        //
        // It used to read "1 RUNNER  2 SOLDIER  3 ARCHER" and had stayed that
        // way through the griffin, the hero, the spells and the cannon — so
        // the only screen whose job is teaching the game taught three of its
        // five units and none of its verbs. A hand-written list is a second
        // copy of the roster, and this file has now had three bugs from two
        // copies of a fact disagreeing. This one cannot disagree.
        owned_.push_back(createCenteredText(world, rosterLine(), 176, 2,
                                            200, 200, 215, kHudLayer));

        if (heroKindIndex() >= 0) {
            owned_.push_back(createCenteredText(
                world, "H  SUMMON YOUR HERO - ONCE A BATTLE, AND ONLY ONCE",
                200, 2, 235, 220, 150, kHudLayer));
        }

        owned_.push_back(createCenteredText(world, spellLine(), 224, 2,
                                            180, 190, 230, kHudLayer));

        owned_.push_back(createCenteredText(
            world, "CLICK THE FIELD TO SHELL IT - DRAG TO LOOK AROUND",
            248, 2, 180, 190, 230, kHudLayer));

        owned_.push_back(createCenteredText(world, "ARCHERS NEED A FRONT LINE",
                                            288, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(
            world, "AND ONLY ARCHERS AND THE HERO REACH THE SKY",
            310, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(world, "BREAK THE ENEMY CASTLE",
                                            332, 2, 170, 170, 195, kHudLayer));

        owned_.push_back(createCenteredText(world, "SPACE TO START", 386, 3,
                                            235, 235, 235, kHudLayer));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 428, 2,
                                            170, 170, 195, kHudLayer));
    }

    // "1 RUNNER  2 SOLDIER  3 ARCHER  4 GRIFFIN", from whatever the bar sells.
    //
    // Slots rather than roster rows, which is the same list the number keys
    // and the spawn bar are bound to — so a unit named here is a unit you can
    // actually send, and a roster too long for the bar does not advertise
    // something that has no button.
    static std::string rosterLine() {
        std::string line;
        for (int slot = 0; slot < visibleButtonCount(); ++slot) {
            const int kind = kindForButton(slot);
            if (kind < 0) continue;
            if (!line.empty()) line += "  ";
            line += std::to_string(slot + 1);
            line += " ";
            line += unitKind(kind).name;
        }
        return line.empty() ? "NO UNITS IN THE ROSTER" : line;
    }

    // "Z METEOR  X HEAL  C RAGE", from the spell table and its own hints.
    static std::string spellLine() {
        std::string line;
        for (int spell = 0; spell < kSpellCount; ++spell) {
            if (!line.empty()) line += "  ";
            line += spellKind(spell).hint;
            line += " ";
            line += spellKind(spell).name;
        }
        return line;
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            scenes.replace(makeStageSelectScene());
        } else if (input.wasKeyPressed(SDL_SCANCODE_Q)) {
            scenes.pop();  // an empty stack is how the engine is told to quit
        }
    }

private:
    std::vector<Entity> owned_;
};

}  // namespace

std::unique_ptr<Scene> makeTitleScene() {
    return std::make_unique<TitleScene>();
}

std::unique_ptr<Scene> makeStageSelectScene() {
    return std::make_unique<StageSelectScene>();
}

std::unique_ptr<Scene> makeArmyScene() {
    return std::make_unique<ArmyScene>();
}

std::unique_ptr<Scene> makeHeroScene() {
    return std::make_unique<HeroScene>();
}

std::unique_ptr<Scene> makePlayScene() {
    return std::make_unique<PlayScene>();
}

}  // namespace lanebattle
