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

#include <SDL.h>

#include <algorithm>
#include <cmath>
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

const UnitKind& kindOf(World& world, Entity entity) {
    const Unit* unit = world.getComponent<Unit>(entity);
    return kindOf(unit ? unit->kind : 1);
}

// --- Sound -----------------------------------------------------------------

void playSpawn() {
    if (audioDevice) audioDevice->play(Waveform::Square, 300.0f, 0.05f, 0.12f);
}

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
Entity findTargetAhead(World& world, Entity attacker, bool leftSide,
                       float attackerX, const UnitKind& stats) {
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
        Team* team = world.getComponent<Team>(other);
        if (!team || team->leftSide != leftSide) continue;

        const UnitKind& otherStats = kindOf(otherUnit->kind);
        if (otherStats.range > stats.range) continue;  // it stops well short of me

        Velocity* velocity = world.getComponent<Velocity>(other);
        if (!velocity || std::fabs(velocity->dx) > 0.01f) continue;  // still moving

        Transform* transform = world.getComponent<Transform>(other);
        if (!transform) continue;

        const float otherCenter = transform->x + otherStats.width / 2.0f;
        const float centreAhead = (otherCenter - moverCenter) * direction;
        if (centreAhead < 0.0f) continue;  // behind me

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
    world.addComponent(shot, Velocity{
        (targetX - fromX) / flight,
        (targetY - fromY) / flight - kCannonGravity * flight / 2.0f,
    });
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
    payload.timeLeft = flight;
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

const std::vector<UnitKind>& unitKinds() { return gUnitKinds; }

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
}

bool loadBalance(const std::string& path) {
    DataFile file;
    if (!file.load(path)) return false;  // nothing found; defaults stand

    for (const DataSection* section : file.all("unit")) {
        const std::string name = section->text("name", "");
        if (name.empty()) continue;  // a row with no name names nothing

        int index = indexOfName(name);
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
        kind.width = section->number("width", kind.width);
        kind.height = section->number("height", kind.height);

        kind.leftR = channel(*section, "left_r", kind.leftR);
        kind.leftG = channel(*section, "left_g", kind.leftG);
        kind.leftB = channel(*section, "left_b", kind.leftB);
        kind.rightR = channel(*section, "right_r", kind.rightR);
        kind.rightG = channel(*section, "right_g", kind.rightG);
        kind.rightB = channel(*section, "right_b", kind.rightB);
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
    for (Entity entity : world.entities()) {
        Unit* unit = world.getComponent<Unit>(entity);
        if (!unit) continue;
        if (kind >= 0 && unit->kind != kind) continue;
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

        Transform* at = world.getComponent<Transform>(entity);
        Polygon* lines = world.getComponent<Polygon>(entity);
        if (!at || !lines) continue;

        // The figure hangs off the bottom centre of the block it decorates.
        at->x = body->x + stats.width / 2.0f;
        at->y = kGroundY;

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

float upgradeCost(int upgrade, int owned) {
    if (upgrade < 0 || upgrade >= kUpgradeCount) return 0.0f;
    const UpgradeKind& kind = kDefaultUpgrades[upgrade];

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
    return kGoldPerSecond + kIncomePerLevel * static_cast<float>(levels);
}

int populationCapFor(const Session& session, bool leftSide) {
    const int levels = upgradesOf(session, leftSide)[static_cast<int>(Upgrade::Supply)];
    return kPopulationCap + kSupplyPerLevel * levels;
}

float castleMaxHealthFor(const Session& session, bool leftSide) {
    const int levels = upgradesOf(session, leftSide)[static_cast<int>(Upgrade::Walls)];
    return kCastleHealth + kWallsPerLevel * static_cast<float>(levels);
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

int visibleButtonCount() {
    return std::min(unitKindCount(), kMaxVisibleButtons);
}

int buttonAt(float screenX, float screenY) {
    if (screenY < kButtonY || screenY > kButtonY + kButtonHeight) return -1;

    for (int index = 0; index < visibleButtonCount(); ++index) {
        const float left = buttonLeft(index);
        if (screenX >= left && screenX <= left + kButtonWidth) return index;
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
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Unit>(entity)) continue;
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
    world.addComponent(unit, Transform{x, kGroundY - stats.height, 0.0f});
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
    world.addComponent(unit, sprite);

    world.addComponent(unit, Team{leftSide});

    Unit component;
    component.kind = kind;
    component.health = stats.health;  // the table is the only source of health
    world.addComponent(unit, component);
    world.getComponent<Unit>(unit)->figure = createFigure(world, unit, sprite);

    // A different pitch per kind, so you can hear what you just sent without
    // looking away from the front line.
    if (audioDevice) {
        audioDevice->play(Waveform::Square, 240.0f + 60.0f * static_cast<float>(kind),
                          0.05f, 0.12f);
    }
    return unit;
}

void setAudioDevice(AudioDevice* audio) { audioDevice = audio; }

namespace {

// --- Scenes ----------------------------------------------------------------

class GameOverScene : public Scene {
public:
    explicit GameOverScene(bool playerWon) : playerWon_(playerWon) {}

    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(
            world, playerWon_ ? "VICTORY" : "DEFEAT", 170, 6,
            playerWon_ ? 140 : 240, playerWon_ ? 230 : 110,
            playerWon_ ? 150 : 110, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "R TO FIGHT AGAIN", 280, 2,
                                            170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 310, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_R)) scenes.pop();
    }

    bool simulatesWorld() const override { return false; }

private:
    bool playerWon_;
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

        // One camera, on its own entity. The renderer picks up the first one
        // it finds; before this game the only thing that ever moved it was
        // Asteroids' screen shake, a few pixels at a time.
        cameraEntity_ = world.createEntity();
        world.addComponent(cameraEntity_, Camera{});

        buildField(world);

        goldText_ = createText(world, "GOLD 150", 16, 14, 3, 235, 220, 150,
                               kHudLayer);
        leftHealthText_ = createText(world, "", 16, 48, 2, 140, 200, 240,
                                     kHudLayer);
        rightHealthText_ = createText(world, "", kWindowWidth - 220, 48, 2,
                                      240, 150, 130, kHudLayer);
        hud_.push_back(goldText_);
        hud_.push_back(leftHealthText_);
        hud_.push_back(rightHealthText_);
        popText_ = createText(world, "", 16, 78, 2, 170, 170, 195, kHudLayer);
        hud_.push_back(popText_);

        buildSpawnBar(world);
        buildUpgradePanel(world);

        cannonText_ = createText(world, "", 16, 112, 2, 220, 200, 140, kHudLayer);
        hud_.push_back(cannonText_);

        hud_.push_back(createText(world, "DRAG OR ARROWS TO LOOK - P PAUSES",
                                  static_cast<int>(kButtonX),
                                  static_cast<int>(kButtonY) - 22, 2,
                                  130, 130, 155, kHudLayer));

        buildMinimap(world);

        // Start looking at your own castle rather than at the origin, so the
        // first frame is not a lurch.
        snapCameraToTarget(world);
    }

    void onExit(World& world) override {
        clearField(world);
        world.destroyLater(sessionEntity_);
        world.destroyLater(cameraEntity_);
        for (Entity entity : hud_) world.destroyLater(entity);
        hud_.clear();
    }

    void onResume(World& world) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session || !session->gameOver) return;  // returning from pause

        clearField(world);
        *session = Session{};
        buildField(world);
        snapCameraToTarget(world);
        overlayShown_ = false;
        shownGold_ = -1;
        shownPopulation_ = -1;
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session) return;

        if (session->gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(session->playerWon));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        session->cannonCooldown = std::max(0.0f, session->cannonCooldown - dt);

        earnGold(*session, dt);
        handleUpgrades(world, *session, input);
        handleSpawning(world, *session, input, dt);
        updateEnemyCannon(world, *session, dt);
        fight(world, *session, dt);
        removeTheDead(world, *session);

        // After the dead are gone, so the camera never chases a corpse for a
        // frame, and after the fight, so the minimap shows this frame's front
        // line rather than last frame's.
        updateCannonballs(world, dt);
        animateUnits(world, dt);
        updateCamera(world, *session, input, dt);
        refreshHud(world, *session);
        refreshMinimap(world);
    }

private:
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
        world.addComponent(left, Castle{});
        field_.push_back(left);

        Entity right = createRect(world, kRightCastleX, kGroundY - kCastleHeight,
                                  kCastleWidth, kCastleHeight, 170, 90, 80,
                                  kFieldLayer);
        world.addComponent(right, Team{false});
        world.addComponent(right, Castle{});
        field_.push_back(right);
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
        // Sky: screen-space, because it genuinely does not move. Three bands
        // standing in for a gradient the renderer cannot draw.
        field_.push_back(makeSky(world, 0.0f, 150.0f, 26, 30, 46));
        field_.push_back(makeSky(world, 150.0f, 120.0f, 34, 38, 54));
        field_.push_back(makeSky(world, 270.0f, kGroundY - 270.0f, 44, 46, 60));

        buildHills(world, kFarParallax, kFarLayer, 150.0f, 320.0f, 54, 58, 78, 11);
        buildHills(world, kMidParallax, kMidLayer, 100.0f, 240.0f, 44, 52, 66, 23);
        buildHills(world, kNearParallax, kNearLayer, 60.0f, 180.0f, 36, 44, 56, 37);
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

            Entity tuft = world.createEntity();
            world.addComponent(tuft, Transform{x + jitter(index + 53, -40.0f, 40.0f),
                                               static_cast<float>(kWindowHeight) - 6.0f,
                                               0.0f});

            Polygon blades;
            blades.points = {Vec2{-9.0f, 0.0f},  Vec2{-4.0f, -height},
                             Vec2{-1.0f, 0.0f},  Vec2{3.0f, -height * 1.2f},
                             Vec2{6.0f, 0.0f},   Vec2{10.0f, -height * 0.8f}};
            blades.closed = false;
            blades.r = 30;
            blades.g = 38;
            blades.b = 44;
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
            if (world.hasComponent<Unit>(entity) ||
                world.hasComponent<Shard>(entity) ||
                world.hasComponent<Cannonball>(entity) ||
                world.hasComponent<Figure>(entity)) {
                world.destroyLater(entity);
            }
        }
        for (Entity entity : field_) world.destroyLater(entity);
        field_.clear();
    }

    static void earnGold(Session& session, float dt) {
        session.gold += goldPerSecondFor(session, true) * dt;
        session.enemyGold +=
            goldPerSecondFor(session, false) * kEnemyIncomeMultiplier * dt;
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
                health->health += kWallsPerLevel;
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
        session.spawnCooldown -= dt;
        if (session.spawnCooldown > 0.0f) return;
        if (countUnits(world, true) >= populationCapFor(session, true)) return;

        // One key per row of the table, so adding a fourth unit type is a
        // table row and one scancode rather than a change to any rule.
        static const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2,
                                            SDL_SCANCODE_3, SDL_SCANCODE_4};
        for (int kind = 0; kind < unitKindCount() && kind < 4; ++kind) {
            if (!input.isKeyDown(keys[kind])) continue;
            if (trySpawn(world, session, kind)) return;
        }

        // A click on the bar does exactly what its key does. `wasMousePressed`
        // rather than `isMouseDown`, or one held click would empty the purse
        // one unit per cooldown for as long as the finger stayed down.
        if (!input.wasMousePressed()) return;

        const int kind = buttonAt(static_cast<float>(input.mouseX()),
                                  static_cast<float>(input.mouseY()));
        if (kind >= 0) trySpawn(world, session, kind);
    }

    // Buys one unit if it can be afforded, and reports whether it did. The one
    // place gold is spent, so the keyboard and the mouse cannot drift apart.
    bool trySpawn(World& world, Session& session, int kind) {
        if (kind < 0 || kind >= unitKindCount()) return false;
        if (session.gold < unitKind(kind).cost) return false;

        session.gold -= unitKind(kind).cost;
        session.spawnCooldown = kSpawnCooldown;
        spawnUnit(world, true, kind);
        return true;
    }

    // The opponent, playing by the same rules from the same purse.
    //
    // It banks for a whole wave before spending any of it, because measuring
    // slice 2 showed that spending on sight loses to anyone who doesn't: the
    // two front lines mirror each other and nothing ever moves. Now that both
    // sides bank, the player has to beat it on composition instead.
    void handleEnemySpawning(World& world, Session& session, float dt) {
        session.enemySpawnTimer -= dt;

        if (session.enemyWaveRemaining <= 0) {
            if (session.enemyGold >= waveCost(session)) {
                session.enemyWaveRemaining = kEnemyWaveSize;
            }
            return;
        }

        if (session.enemySpawnTimer > 0.0f) return;
        if (countUnits(world, false) >= populationCapFor(session, false)) return;

        const int kind = kEnemyComposition[session.enemyWaveIndex %
                                           kEnemyCompositionLength];
        if (session.enemyGold < unitKind(kind).cost) return;

        session.enemyGold -= unitKind(kind).cost;
        session.enemySpawnTimer = kSpawnCooldown;
        session.enemyWaveIndex =
            (session.enemyWaveIndex + 1) % kEnemyCompositionLength;
        --session.enemyWaveRemaining;
        spawnUnit(world, false, kind);
    }

    // What the next whole wave costs, reading forward through the cycle from
    // wherever it currently is.
    static float waveCost(const Session& session) {
        float total = 0.0f;
        for (int step = 0; step < kEnemyWaveSize; ++step) {
            const int index = (session.enemyWaveIndex + step) %
                              kEnemyCompositionLength;
            total += unitKind(kEnemyComposition[index]).cost;
        }
        return total;
    }

    // Each unit either walks or fights, never both.
    void fight(World& world, Session& session, float dt) {
        // Gathered first, because attacking creates shards and killing blows
        // queue deletions — neither of which should happen while iterating
        // the live entity list.
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

            const UnitKind& stats = kindOf(unit->kind);
            const Entity target =
                findTargetAhead(world, attacker, team->leftSide, at->x, stats);

            if (target == kInvalidEntity) {
                // Nothing in reach. March, unless one of our own is in the
                // way — in which case wait our turn rather than standing
                // inside them.
                const bool queued = blockedByFriendly(world, attacker,
                                                      team->leftSide, at->x, stats);
                velocity->dx = queued ? 0.0f : stats.speed * facing(team->leftSide);
                continue;
            }

            velocity->dx = 0.0f;
            unit->timeUntilAttack -= dt;
            if (unit->timeUntilAttack > 0.0f) continue;

            unit->timeUntilAttack = stats.attackDelay;
            unit->swing = 1.0f;  // starts the arm through its arc

            if (Unit* victim = world.getComponent<Unit>(target)) {
                victim->health -= stats.damage;
                playHit();
            } else if (Castle* castle = world.getComponent<Castle>(target)) {
                castle->health -= stats.damage;
                playCastleHit();
                if (castle->health <= 0.0f) {
                    Team* castleTeam = world.getComponent<Team>(target);
                    session.gameOver = true;
                    // The player holds the left castle, so the right one
                    // falling is a win.
                    session.playerWon = castleTeam && !castleTeam->leftSide;
                }
            }
        }
    }

    void removeTheDead(World& world, Session& session) {
        for (Entity entity : world.entities()) {
            Unit* unit = world.getComponent<Unit>(entity);
            if (!unit || unit->health > 0.0f) continue;

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
            }

            if (Transform* at = world.getComponent<Transform>(entity)) {
                const UnitKind& stats = kindOf(unit->kind);
                Sprite* sprite = world.getComponent<Sprite>(entity);
                spawnShards(world, at->x + stats.width / 2.0f,
                            at->y + stats.height / 2.0f,
                            sprite ? sprite->r : 200, sprite ? sprite->g : 200,
                            sprite ? sprite->b : 200);
            }
            playDeath();
            world.destroyLater(entity);
            if (unit->figure != kInvalidEntity) world.destroyLater(unit->figure);
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
            Velocity* velocity = world.getComponent<Velocity>(entity);
            if (velocity) velocity->dy += kCannonGravity * dt;

            shot.timeLeft -= dt;
            if (shot.timeLeft <= 0.0f) landed.push_back(entity);
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
        }

        spawnShards(world, blastX, blastY, 250, 210, 140);
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
        const int nextUpgrade = cheapestUpgradeFor(session, false);
        const float reserve =
            waveCost(session) +
            (nextUpgrade >= 0
                 ? upgradeCost(nextUpgrade, session.enemyUpgrades[nextUpgrade])
                 : 0.0f);
        if (session.enemyGold < kCannonCost + reserve) return;

        session.enemyGold -= kCannonCost;
        session.enemyCannonCooldown = kCannonCooldown;
        fireCannon(world, false, from, cannonY(), target, kGroundY - 10.0f);
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

        // One button, two verbs. A press on the field is undecided: move more
        // than a few pixels and it is a camera drag, release without moving
        // and it is a cannon shot. A press that lands on a UI element is
        // neither — that one belongs to the button underneath it, or every
        // click on the spawn bar would also nudge the camera.
        if (input.wasMousePressed() && buttonAt(mouseX, mouseY) < 0 &&
            upgradeAt(mouseX, mouseY) < 0) {
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
            if (!session.dragging) fireAt(world, session, session.pressX,
                                          session.pressY);
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

        for (int kind = 0; kind < shown; ++kind) {
            const UnitKind& stats = unitKind(kind);
            const float left = buttonLeft(kind);

            // Three pieces per button: the plate, a fill that shrinks as the
            // cooldown runs, and the label. The fill is drawn over the plate
            // and under the text, which the layer sort already guarantees
            // because entity ids increase in creation order within a layer.
            buttonPlate_[kind] =
                makeScreenRect(world, left, kButtonY, kButtonWidth,
                               kButtonHeight, 34, 38, 48, kHudLayer);
            buttonFill_[kind] =
                makeScreenRect(world, left, kButtonY + kButtonHeight - 4.0f,
                               kButtonWidth, 4.0f, stats.leftR, stats.leftG,
                               stats.leftB, kHudLayer);

            const std::string label =
                std::to_string(kind + 1) + " " + stats.name;
            buttonName_[kind] = createText(
                world, label, static_cast<int>(left) + 8,
                static_cast<int>(kButtonY) + 8, 2, stats.leftR, stats.leftG,
                stats.leftB, kHudLayer);
            buttonCost_[kind] = createText(
                world, std::to_string(static_cast<int>(stats.cost)),
                static_cast<int>(left) + 8, static_cast<int>(kButtonY) + 26, 2,
                220, 200, 140, kHudLayer);

            world.getComponent<Text>(buttonName_[kind])->screenSpace = true;
            world.getComponent<Text>(buttonCost_[kind])->screenSpace = true;

            hud_.push_back(buttonPlate_[kind]);
            hud_.push_back(buttonFill_[kind]);
            hud_.push_back(buttonName_[kind]);
            hud_.push_back(buttonCost_[kind]);
        }
    }

    void refreshSpawnBar(World& world, const Session& session, int fielded) {
        for (int kind = 0; kind < visibleButtonCount(); ++kind) {
            const UnitKind& stats = unitKind(kind);
            const bool affordable = session.gold >= stats.cost &&
                                    fielded < kPopulationCap;

            if (Text* name = world.getComponent<Text>(buttonName_[kind])) {
                name->r = affordable ? stats.leftR : 95;
                name->g = affordable ? stats.leftG : 95;
                name->b = affordable ? stats.leftB : 110;
            }
            if (Text* cost = world.getComponent<Text>(buttonCost_[kind])) {
                cost->r = affordable ? 220 : 95;
                cost->g = affordable ? 200 : 95;
                cost->b = affordable ? 140 : 110;
            }
            if (Sprite* plate = world.getComponent<Sprite>(buttonPlate_[kind])) {
                plate->r = affordable ? 44 : 28;
                plate->g = affordable ? 50 : 32;
                plate->b = affordable ? 64 : 40;
            }

            // The fill is the shared spawn cooldown draining left to right,
            // so the bar shows *when* you can send as well as *what*.
            if (Sprite* fill = world.getComponent<Sprite>(buttonFill_[kind])) {
                const float remaining =
                    std::max(0.0f, session.spawnCooldown) / kSpawnCooldown;
                fill->width = static_cast<int>(kButtonWidth * (1.0f - remaining));
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
                text->value = std::string(kDefaultUpgrades[index].name) + " " +
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

        if (Text* text = world.getComponent<Text>(cannonText_)) {
            const bool ready = session.cannonCooldown <= 0.0f &&
                              session.gold >= kCannonCost;
            text->value =
                ready ? "CANNON READY - CLICK THE FIELD - " +
                            std::to_string(static_cast<int>(kCannonCost)) + "G"
                      : "CANNON " + std::to_string(
                                        static_cast<int>(session.cannonCooldown) + 1);
            text->r = ready ? 220 : 110;
            text->g = ready ? 200 : 110;
            text->b = ready ? 140 : 125;
        }

        updateHealthText(world, leftHealthText_, "YOU  ", true);
        updateHealthText(world, rightHealthText_, "ENEMY ", false);
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
    Entity upgradePlate_[kUpgradeCount] = {};
    Entity upgradeText_[kUpgradeCount] = {};
    Entity minimapLeftCastle_ = kInvalidEntity;
    Entity minimapRightCastle_ = kInvalidEntity;
    Entity minimapLeftFront_ = kInvalidEntity;
    Entity minimapRightFront_ = kInvalidEntity;
    std::vector<Entity> field_;
    std::vector<Entity> hud_;
    int shownGold_ = -1;
    int shownPopulation_ = -1;
    bool overlayShown_ = false;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createCenteredText(world, "LANE BATTLE", 110, 8,
                                            220, 200, 140, kHudLayer));
        owned_.push_back(createCenteredText(world, "1 RUNNER  2 SOLDIER  3 ARCHER",
                                            210, 2, 200, 200, 215, kHudLayer));
        owned_.push_back(createCenteredText(world, "ARCHERS NEED A FRONT LINE",
                                            240, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(world, "BREAK THE ENEMY CASTLE",
                                            270, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(world, "SPACE TO START", 350, 3,
                                            235, 235, 235, kHudLayer));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 400, 2,
                                            170, 170, 195, kHudLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            scenes.replace(makePlayScene());
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

std::unique_ptr<Scene> makePlayScene() {
    return std::make_unique<PlayScene>();
}

}  // namespace lanebattle
