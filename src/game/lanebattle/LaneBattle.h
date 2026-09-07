#pragma once
// ---------------------------------------------------------------------------
// LaneBattle.h — a castle-versus-castle lane battler, in the genre of Cartoon
// Wars and Age of War.
//
// The whole game is one loop: gold accrues, you spend it to send units right,
// they walk until they meet an enemy, they fight, and whoever's castle falls
// first loses. Unit types, upgrades and spells are all decoration on that.
//
// Slice 1 built only that loop, on purpose, and needed NO new engine code at
// all: Transform and Velocity for walking, MovementSystem to do the walking,
// Sprite to draw, Lifetime for the death debris, destroyLater so a unit can
// die in the middle of the loop that killed it, and the scene stack for
// title/play/pause/game-over.
//
// Slice 2 widened the battlefield to two and a half screens, which is the
// first time in this project that world coordinates and screen coordinates
// have differed. That pulled exactly one thing out of the engine — the
// world-to-screen rule itself, moved into engine/View.h so it could be tested
// without a window — and everything else it needed was already there.
//
// As always, only what a test or main() needs is exposed.
// ---------------------------------------------------------------------------

#include <memory>
#include <string>
#include <vector>

#include "engine/Audio.h"
#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Scene.h"

namespace lanebattle {

// --- Layout ----------------------------------------------------------------

// The window is the VIEW. The battlefield is wider than it, which is the whole
// point of this slice: until now every game in this project fit its world into
// its window, so world coordinates and screen coordinates were the same number
// and the renderer's camera was never really exercised.
constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 540;

constexpr float kWorldWidth = 2400.0f;  // two and a half screens

// Everything stands on this line. A lane battler is really a one-dimensional
// game with a picture drawn around it: only x ever decides anything.
constexpr float kGroundY = 420.0f;

constexpr float kCastleWidth = 70.0f;
constexpr float kCastleHeight = 130.0f;
constexpr float kCastleMargin = 30.0f;
constexpr float kCastleHealth = 800.0f;

// --- The roster ------------------------------------------------------------
//
// Slices 1 and 2 had one unit type, and measuring a whole battle showed why
// that was fatal: with one type, the only decision is *when* to spend, both
// sides trade evenly, and a player who mirrors the opponent deadlocks the
// field forever. Three types make the question "when AND what", which is the
// smallest change that puts a real decision in the loop.
//
// This is a table, deliberately. Every number that balances the game is in one
// place and in rows rather than scattered through the rules, which is how the
// genre does it — the reference game keeps its equivalent in files exported
// from a spreadsheet. When there are enough rows that recompiling to tweak one
// hurts, the table moves to a file and nothing else has to change. That is a
// later slice, and it is not this one: six numbers do not hurt yet.
struct UnitKind {
    const char* name;
    float cost;
    float health;
    float damage;
    float range;         // how close before it stops to fight
    float attackDelay;   // seconds between blows
    float speed;         // pixels per second

    // How long THIS kind's button is unavailable after sending one. Each kind
    // has its own, which is what the genre does and is not the small detail it
    // looks like.
    //
    // With a single shared cooldown the only limit on spending was gold, so a
    // banked purse went entirely into whichever unit was best and composition
    // was a preference rather than a constraint. Per-kind timers mean a large
    // purse CANNOT be spent on one type: to use it you have to send something
    // else, which is the whole point of having more than one.
    //
    // They mostly do not bind at base income — a soldier takes four seconds to
    // afford and two to recharge — and start mattering exactly when the player
    // has money to burn, which is when the interesting decision exists.
    float cooldown;

    float width;
    float height;
    unsigned char leftR, leftG, leftB;     // your colours
    unsigned char rightR, rightG, rightB;  // theirs
};

// The roster's hard ceiling. A data file may add unit types, but each side
// needs one cooldown timer per kind stored on the Session, and an unbounded
// component is a worse thing to own than a documented limit.
constexpr int kMaxUnitKinds = 16;

// The three roles the genre is built on, and the shape of each:
//
//   RUNNER  — cheap and fast, dies to anything, wins on gold-for-damage.
//   SOLDIER — the slice-1 unit. Beats a runner one-to-one and loses to two.
//   ARCHER  — outranges everything, but cannot survive being reached. Useless
//             alone; the reason to own a front line.
//
// These numbers are a starting point that was then measured rather than
// argued about: see the matchup probe in the tests.
// These are the DEFAULTS, not the roster. `assets/lanebattle/units.txt`
// overrides any of them and may add more; a missing or broken file leaves
// these exactly as they are, which is why the game still runs from a bare
// build directory and why the tests are deterministic without touching a
// filesystem. Read the live roster through `unitKind()` — never through this.
constexpr UnitKind kDefaultUnitKinds[] = {
    // name      cost   hp     dmg   range  delay  speed  cool   w      h     yours          theirs
    {"RUNNER",   35.0f, 60.0f,  8.0f,  30.0f, 0.45f, 150.0f, 1.1f, 16.0f, 26.0f, 150, 215, 255, 255, 175, 150},
    {"SOLDIER",  60.0f, 110.0f, 14.0f, 34.0f, 0.60f,  95.0f, 1.9f, 24.0f, 36.0f, 110, 190, 240, 235, 130, 110},
    {"ARCHER",   95.0f, 55.0f,  20.0f, 135.0f, 0.95f,  70.0f, 3.0f, 18.0f, 34.0f,  90, 140, 210, 200,  95, 130},
};
constexpr int kDefaultUnitKindCount =
    static_cast<int>(sizeof(kDefaultUnitKinds) / sizeof(kDefaultUnitKinds[0]));

// The live roster. A file may override rows and append new ones, so both the
// contents and the count are runtime values now.
const UnitKind& unitKind(int kind);  // clamped; never indexes off the end
int unitKindCount();

// Reads `path` (resolved against the executable) over the top of the roster.
// Returns false if the file was not found, in which case nothing changed and
// the defaults stand. Each `[unit]` section either updates the row whose name
// it matches or appends a new one, and any field it omits keeps its previous
// value — so a file that only wants to make archers cheaper says exactly that
// and nothing else.
bool loadBalance(const std::string& path);

// Puts the roster back to the compiled-in defaults. Exists for the tests: the
// roster is global mutable state, and a test that loads a file must not change
// the meaning of the one that runs after it.
void resetBalance();

// The path the game loads at startup.
constexpr const char* kBalancePath = "assets/lanebattle/units.txt";

// The widest and tallest of them, for the few places that need a bound before
// knowing which kind they are dealing with.
constexpr float kMaxUnitWidth = 24.0f;

// --- Tuning ----------------------------------------------------------------

constexpr float kStartingGold = 150.0f;
constexpr float kGoldPerSecond = 14.0f;
// The floor under every unit's own cooldown, so one keypress can never become
// an army even if a data file sets a cooldown of zero.
constexpr float kMinSpawnCooldown = 0.35f;

// Killing something pays, as a fraction of what it cost its owner.
//
// This is the mechanic the first three attempts at this game were missing, and
// without it the design cannot work. With income as the only source of gold,
// two competent players earn identically no matter what happens on the field,
// so a won fight buys nothing and the front line returns to the middle — which
// is exactly what measuring slices 1, 2 and the first cut of 3 all showed:
// good play drew, bad play lost, and nothing in between existed.
//
// Paying for kills makes a favourable trade compound. Killing a 95-gold archer
// with two 35-gold runners is now worth doing twice, and an advantage on the
// field turns into an advantage in the purse, which turns back into more
// units. That loop is what lets skill decide a battle instead of arithmetic.
constexpr float kKillRewardFraction = 0.45f;

// --- The castle cannon -----------------------------------------------------
//
// Until now there was nothing to do between spending decisions but watch. The
// cannon is the genre's answer: a slow, aimed shot that rewards paying
// attention to where the fighting actually is.
//
// Aiming is a click on the field, which is the first thing in this project to
// need `screenToWorld`. That inverse was written in slice 4 and had no caller
// at all — the spawn bar is screen-space, so it hit-tests against the cursor
// directly. A shot is different: the click is in window pixels and the ground
// it lands on is up to a screen and a half away.
//
// A click that turns into a drag is not a shot. Press, move more than a few
// pixels, and it is a camera drag; press and release in about the same place
// and it is a shot. That is the standard tap-versus-drag rule and it is what
// lets one button do both without a modifier key.
// A shot COSTS GOLD, which is the whole reason this is a decision rather than
// a wall.
//
// The first version was free, on a cooldown, permanent and perfectly aimed —
// and measuring it showed the consequence immediately: a mixed army that won
// in 195 seconds lost in 247, and taking the cannon's damage away restored the
// old result exactly. Free defensive damage that never runs out makes the last
// stretch in front of a castle a killing field nobody can cross, so defending
// beats attacking for both sides and every game ends 800-800.
//
// Charging for it turns "how much damage does the cannon do" into "how much
// damage per gold", which is a number that can be compared against a unit and
// therefore balanced. A side that shells constantly fields a smaller army,
// which is exactly the trade that should exist.
constexpr float kCannonCost = 30.0f;
constexpr float kCannonDamage = 26.0f;
constexpr float kCannonBlastRadius = 46.0f;
constexpr float kCannonCooldown = 3.2f;   // seconds between shots

// Deliberately less than half the distance to the middle: the cannon defends
// the approach to your own castle, it does not contest the field. At 780 it
// covered a third of the world from each end and there was nowhere safe left.
constexpr float kCannonRange = 420.0f;
constexpr float kCannonFlightTime = 0.85f;
constexpr float kCannonGravity = 900.0f;  // pixels per second per second

// --- In-battle upgrades ----------------------------------------------------
//
// Three things to spend gold on that are not units, so a full population cap
// stops being a reason to sit idle. Costs rise geometrically, which is what
// stops the right answer being "buy everything immediately".
//
// Both sides buy them. That symmetry has been load-bearing twice already: an
// opponent with a different economy made slice 1 unwinnable, and an opponent
// with no strategy made slice 3 a mirror. An opponent that cannot upgrade
// would lose every long game by construction.
// The roster is a runtime list because a data file may add unit types: the
// game treats every row the same way, so a fourth kind needs a table row and
// nothing else. Upgrades are not like that. Each one has its own rule written
// in code — INCOME changes a rate, WALLS heals a castle, SUPPLY raises a cap —
// and there is no generic "apply upgrade N". So their COUNT is fixed while
// their NUMBERS are data, and that is a distinction rather than an
// inconsistency: what a file can change is what the code treats uniformly.
enum class Upgrade { Income, Walls, Supply, Count };
constexpr int kUpgradeCount = static_cast<int>(Upgrade::Count);

struct UpgradeKind {
    const char* name;
    float baseCost;
    float costGrowth;  // multiplied in per level already bought
    float effect;      // per level: gold/second, castle health, or unit slots
};

constexpr UpgradeKind kDefaultUpgrades[] = {
    {"INCOME", 120.0f, 1.65f,   4.0f},   // extra gold per second
    {"WALLS",  150.0f, 1.70f, 260.0f},   // extra castle health, healed on purchase
    {"SUPPLY", 200.0f, 1.85f,   3.0f},   // extra population slots
};

// The live upgrade table, which `assets/lanebattle/units.txt` may retune
// through `[upgrade]` sections exactly as it retunes units.
const UpgradeKind& upgradeKind(int upgrade);

// How many units one side may have on the field at once, before SUPPLY.
//
// This is the answer to "why isn't sending more always right?", and without it
// there is no answer: measuring slice 2 showed that banking two units' worth
// won, and banking six won barely faster, so there was nothing to think about
// past the first decision. A cap makes every slot spent an opportunity cost,
// which is what turns composition into a choice.
constexpr int kPopulationCap = 10;

// Units queue rather than standing inside one another. Without this the whole
// army piles onto the same pixel and fights as one enormous unit, which is
// what made massing unconditionally correct.
constexpr float kRankGap = 6.0f;             // clear space between queued units

// The opponent plays by exactly the same rules: the same purse, the same
// income, the same costs, the same cooldown. Its only advantage is that it
// never forgets to spend.
//
// It began as a free unit every three seconds, which quietly made the game
// unwinnable — the enemy out-produced the player by half again, and no amount
// of tuning the player's economy could fix an opponent that didn't have one.
// Symmetry costs nothing and makes the difficulty a single number to change.
constexpr float kEnemyIncomeMultiplier = 1.0f;

// --- Stages ----------------------------------------------------------------
//
// A campaign rather than one endless skirmish. Each stage is the same battle
// with the opponent's three levers set differently: how fast it earns, how
// much castle it has to chew through, and what it sends.
//
// That last one is what makes the stages feel different rather than merely
// harder. A stage fielding only soldiers is answered by archers behind a thin
// line; one fielding archers of its own has to be rushed before they set up.
// Turning up a difficulty number would have produced eight identical fights.
constexpr int kMaxComposition = 12;

struct StageKind {
    const char* name;
    float enemyIncome;        // multiplier on the base gold rate
    float enemyCastleHealth;
    int waveSize;             // units it banks for before spending
    const char* composition;  // roster indices, e.g. "1,1,2"
};

// These numbers were measured, not chosen. The first attempt ran 0.75 to 1.60
// on income and 600 to 1500 on castle health, which produced a campaign whose
// first stage a naive army could only draw and whose last three **no strategy
// could win at all**. Playing every stage with three different armies said so
// in one run; reading the table would never have.
// The COMPOSITION escalates as much as the numbers do, and that is not
// decoration — measuring showed it matters more than either dial.
//
// The second attempt raised income and castle health smoothly but let the
// compositions wander, and stage three came out a wall that nothing could beat
// while stage four was comfortable. Stage three fielded a lean "1,1,2" and
// stage four a "1,1,2,0" padded with a runner: a weak unit in the cycle
// spends gold that would otherwise have bought a soldier, so the higher
// income bought a worse army. Early stages are diluted on purpose; the last
// ones are lean.
constexpr StageKind kDefaultStages[] = {
    {"THE BORDER",     0.40f,  350.0f, 2, "1,0"},
    {"RIVER CROSSING", 0.55f,  450.0f, 2, "1,1,0"},
    {"THE FOOTHILLS",  0.68f,  550.0f, 3, "1,1,0,2"},
    {"OLD ROAD",       0.80f,  650.0f, 3, "1,1,2,0"},
    {"THE PASS",       0.90f,  750.0f, 3, "1,1,2"},
    {"BLACK FIELD",    1.00f,  850.0f, 4, "1,2,1,2,0"},
    {"THE GATES",      1.05f,  900.0f, 4, "1,1,2,0,2"},
    {"THE KEEP",       1.15f, 1000.0f, 4, "1,1,2,2"},
};
constexpr int kDefaultStageCount =
    static_cast<int>(sizeof(kDefaultStages) / sizeof(kDefaultStages[0]));
constexpr int kMaxStages = 24;

const StageKind& stageKind(int stage);
int stageCount();

// --- Permanent upgrades ----------------------------------------------------
//
// The in-battle upgrades are spent from a purse that resets every battle. These
// are bought between battles from money the campaign paid out, and they never
// go away — which is the difference between a game you can finish in one
// sitting and one worth coming back to.
//
// Three of them, deliberately touching things the in-battle upgrades do not,
// so the two systems are not the same choice at different speeds.
enum class Perk { Damage, Fortify, Purse, Count };
constexpr int kPerkCount = static_cast<int>(Perk::Count);

struct PerkKind {
    const char* name;
    const char* effectText;
    float baseCost;
    float costGrowth;
    float effect;  // per level
};

constexpr PerkKind kDefaultPerks[] = {
    {"WEAPONS",  "+10% DAMAGE",   200.0f, 1.55f, 0.10f},
    {"RAMPARTS", "+150 CASTLE",   180.0f, 1.55f, 150.0f},
    {"TREASURY", "+40 START GOLD", 160.0f, 1.55f, 40.0f},
};

const PerkKind& perkKind(int perk);
float perkCost(int perk, int owned);

// What a stage pays for winning it. Later stages pay more, and the first clear
// of one pays double — so pushing forward is worth more than farming a stage
// you have already beaten, without ever forbidding the farming.
constexpr float kStageRewardBase = 90.0f;
constexpr float kStageRewardPerStage = 55.0f;
constexpr float kFirstClearBonus = 2.0f;

float stageReward(int stage, bool firstClear);

// How far through the campaign the player has got, and what they own.
//
// A separate component from Session because Session is one battle and this
// outlives them: it is created once and read by the stage-select screen, the
// play scene, and the victory that unlocks the next stage.
struct Campaign {
    int stagesUnlocked = 1;
    int currentStage = 0;
    int bank = 0;                    // gold carried between battles
    int perks[kPerkCount] = {};
    bool cleared[kMaxStages] = {};   // for the first-clear bonus
};

Campaign& campaignOf(engine::World& world);  // created on first use

// --- Saving ----------------------------------------------------------------
//
// Written to the per-user location SDL reports, not next to the executable:
// the folder a game is installed into is frequently read-only.
//
// A missing save is a new campaign, and a corrupt one is treated the same way.
// There is nothing here worth refusing to start over.
bool saveCampaign(const Campaign& campaign);
bool loadCampaign(Campaign& campaign);
std::string savePath();

// Points saving somewhere else. For tests, which must not write over a real
// player's campaign — and for anyone who wants two of them.
void setSavePath(const std::string& path);

// --- The view --------------------------------------------------------------
//
// The camera rides with your front line, because that is where your decisions
// land. With nothing on the field it drifts home to your own castle, which
// also happens to be the right place to be looking when you have just lost an
// army and something is walking towards your gate.
//
// LEFT and RIGHT take the view off the leash so you can scout; letting go
// hands it back after a moment. That override is not a luxury on a field this
// wide — without it the player cannot look at their own castle while the
// fighting is happening two screens away.
constexpr float kCameraFollowRate = 4.0f;    // how fast it catches up, per second
constexpr float kFreeLookSpeed = 640.0f;     // pixels per second while steering
constexpr float kFreeLookHold = 1.2f;        // seconds before following resumes

// The furthest left the view can sit is 0 and the furthest right is this: the
// camera stops at the edges of the world rather than showing empty space past
// them.
constexpr float kCameraMaxX = kWorldWidth - static_cast<float>(kWindowWidth);

// --- The minimap -----------------------------------------------------------
//
// A strip across the top standing in for the whole battlefield, with a marker
// for each castle and each side's front line. On a one-screen field this would
// be clutter; on a field two and a half screens wide it is the only way to
// know a push is happening while you are looking somewhere else.
// --- Animation -------------------------------------------------------------
//
// Units are drawn as a coloured block with an articulated stick figure over
// the top of it: two legs that swing while walking, and an arm that swings
// through an arc when a blow lands. The block is the silhouette and reads at a
// glance; the figure is what makes it obvious whether something is marching or
// fighting, which was previously visible only as a rectangle that had stopped.
//
// It is built out of `Polygon`, whose points are a plain vector this rebuilds
// every frame. No sprite sheets, no art files, and — the reason this route was
// taken over sprites — nothing to draw before it works.
//
// That does mean this slice does NOT deliver the `Animation` component and
// `Sprite.flip` the roadmap predicted. Those are for frame-based sprite
// animation, and building them now would be building for art that does not
// exist. They stay on the roadmap for whenever it does.
constexpr float kWalkCycleRate = 0.10f;   // radians of leg swing per pixel walked
constexpr float kLegSwing = 6.0f;         // how far a foot travels from centre
constexpr float kSwingDecayRate = 5.0f;   // how fast an attack swing plays out

// --- The spawn bar ---------------------------------------------------------
//
// One button per row of the roster, along the bottom of the screen. The number
// keys still work and always will — they are faster once you know the roster,
// and losing them to add a mouse would be a downgrade — but a bar is how this
// genre is actually played, and it is the only way a new player learns what
// the three types cost without reading a README.
//
// Buttons are screen-space, so they stay put while the field scrolls beneath
// them, and they are hit-tested directly against the cursor with no camera
// involved. That is the whole reason `screenToWorld` is *not* used here: a
// screen-space element is already in the space the mouse reports.
constexpr float kButtonWidth = 130.0f;
constexpr float kButtonHeight = 46.0f;
constexpr float kButtonGap = 10.0f;
constexpr float kButtonY = kWindowHeight - kButtonHeight - 14.0f;
constexpr float kButtonX = 16.0f;

// Where button `index` sits. Kept here rather than in the .cpp because both
// the game and its tests need to know — a test that clicks a button has to
// work out where it is, and hard-coding that in two places is how a UI test
// stops testing the UI.
constexpr float buttonLeft(int index) {
    return kButtonX + static_cast<float>(index) * (kButtonWidth + kButtonGap);
}

// How many buttons fit across the bottom before they run off the edge. The
// roster's length is a runtime value now, so this is a real limit rather than
// an arithmetic curiosity: a data file with ten unit types would otherwise
// draw four of them into the void.
constexpr int kMaxVisibleButtons =
    static_cast<int>((static_cast<float>(kWindowWidth) - kButtonX + kButtonGap) /
                     (kButtonWidth + kButtonGap));

// The roster clamped to what fits. Buttons past this have no plate, no label
// and no hit box — but their number keys still work, so nothing is unreachable.
int visibleButtonCount();

// Dragging the field scrolls the view, which is the genre's other mouse verb.
// A few pixels of slop before a press counts as a drag, so a slightly shaky
// click on a button is still a click.
constexpr float kDragThreshold = 4.0f;

constexpr float kMinimapX = 300.0f;
constexpr float kMinimapY = 20.0f;
constexpr float kMinimapWidth = 360.0f;
constexpr float kMinimapHeight = 12.0f;
constexpr float kMinimapMarkerWidth = 6.0f;

// --- Scenery ---------------------------------------------------------------
//
// Three bands of distance behind the fighting and one in front of it, each
// sliding past at its own rate. Without them a two-and-a-half-screen field
// scrolls past a flat colour and reads as a corridor; with them it reads as a
// place, and — the practical part — you can tell how far you have scrolled
// without looking at the minimap.
//
// The numbers are the fraction of the camera's movement each band gets. The
// ground is 1.0 by definition; the foreground is over 1.0, which is what sells
// depth in the other direction and is the case a boolean could never express.
constexpr float kFarParallax = 0.18f;
constexpr float kMidParallax = 0.45f;
constexpr float kNearParallax = 0.72f;
constexpr float kForeParallax = 1.30f;

// A band at parallax p slides `kCameraMaxX * p` pixels over a full sweep of
// the camera, so it needs to be that much wider than the window to avoid
// running out and showing the void behind it.
constexpr float bandWidth(float parallax) {
    return static_cast<float>(kWindowWidth) + kCameraMaxX * parallax + 40.0f;
}

// Layers are drawn low to high, so scenery behind the fight is negative and
// the foreground sits above everything except the HUD.
constexpr int kSkyLayer = -4;
constexpr int kFarLayer = -3;
constexpr int kMidLayer = -2;
constexpr int kNearLayer = -1;

constexpr int kFieldLayer = 0;
constexpr int kForeLayer = 1;
constexpr int kHudLayer = 5;
constexpr int kOverlayLayer = 10;
constexpr int kOverlayTextLayer = 11;

// --- Components ------------------------------------------------------------

// Which side something belongs to. Left walks right; right walks left.
struct Team {
    bool leftSide = true;
};

struct Unit {
    int kind = 1;  // an index into kUnitKinds
    float health = 0.0f;
    float timeUntilAttack = 0.0f;

    // Where the walk cycle has got to, advanced by distance travelled rather
    // than by time — so a runner's legs move faster than a soldier's for free,
    // and nothing slides with its feet still.
    float phase = 0.0f;

    // Counts down from 1 to 0 after a blow lands, driving the arm through its
    // arc. Separate from timeUntilAttack because the swing should be quick and
    // the same length for every unit, while the delay between blows is a
    // balance number that differs per kind.
    float swing = 0.0f;

    // The stick figure drawn over this unit, so a death can take it along in
    // the same frame. Relying on the orphan sweep alone left the figure alive
    // for a frame after its unit — invisible on screen, but it meant "one
    // figure per unit" was briefly false during every fight, and a test that
    // has to tolerate a transient cannot catch a real leak.
    engine::Entity figure = engine::kInvalidEntity;
};

// The stick figure drawn over a unit. One per unit, owning no state of its own
// — everything about it is recomputed each frame from its owner.
//
// It is a separate entity rather than more fields on the unit because the
// renderer draws components, and a unit needs to be both a filled block and a
// line drawing. Keeping the link one-way (figure knows its owner, not the
// reverse) means nothing has to remember to tidy up: a figure whose owner has
// gone is an orphan, and the animation pass sweeps orphans.
struct Figure {
    engine::Entity owner = engine::kInvalidEntity;
};

struct Castle {
    float health = kCastleHealth;
};

// Debris from a dying unit, so a death reads as an event rather than a
// disappearance. Uses Lifetime to clean itself up.
struct Shard {};

// A shot in the air. It carries which side fired it so a cannonball cannot
// kill its own army, and where it is aimed so it knows when it has arrived —
// a fixed flight time is simpler and more accurate than watching for the
// ground, and it means a click always lands exactly where it was clicked.
struct Cannonball {
    bool leftSide = true;
    float timeLeft = kCannonFlightTime;
};

// The whole battle's state, on one entity — the singleton-component pattern
// used by the other games, which is what lets a test read the gold without
// the scene exposing anything.
struct Session {
    float gold = kStartingGold;
    float enemyGold = kStartingGold;
    bool gameOver = false;
    bool playerWon = false;

    // One timer per unit kind, per side. This is what makes a button bar a
    // decision: a full purse cannot be poured into a single type, so spending
    // it means sending something else.
    float spawnCooldowns[kMaxUnitKinds] = {};
    float enemySpawnCooldowns[kMaxUnitKinds] = {};

    // Where the opponent is in its composition cycle, and how much of the
    // current wave it still owes. Zero means it is banking for the next one.
    int enemyWaveIndex = 0;
    int enemyWaveRemaining = 0;

    // The stage's settings, copied in when the battle starts. Held here rather
    // than read from the stage table on every use, so a battle is decided by
    // one snapshot taken at the start — and so a test can set up a fight
    // without inventing a stage to hold it.
    int composition[kMaxComposition] = {1, 1, 2, 0, 1, 0};
    int compositionLength = 6;
    int enemyWaveSize = 3;
    float enemyIncome = 1.0f;

    // Counts down while the player is steering the view by hand. Above zero
    // the camera obeys the arrow keys or the drag; at zero it goes back to
    // following.
    float freeLookSeconds = 0.0f;

    // A drag in progress: where it started, and whether it has moved far
    // enough to count as one rather than as a click.
    bool dragging = false;
    float dragStartX = 0.0f;
    float dragStartCameraX = 0.0f;

    // A press that has not yet decided whether it is a drag or a shot.
    bool pressPending = false;
    float pressX = 0.0f;
    float pressY = 0.0f;

    float cannonCooldown = 0.0f;
    float enemyCannonCooldown = kCannonCooldown;  // it does not open fire instantly

    // How many of each upgrade each side has bought.
    int upgrades[kUpgradeCount] = {};
    int enemyUpgrades[kUpgradeCount] = {};
};

// --- Queries (used by the game and by its tests) ---------------------------

Session* findSession(engine::World& world);
engine::Entity findCastle(engine::World& world, bool leftSide);
int countUnits(engine::World& world, bool leftSide);

// The view. Exposed because the camera is now real game state — where it is
// pointing decides what the player can see — and a test can check it without
// a window ever opening.
engine::Camera* findCamera(engine::World& world);

// Where a side's advance has reached: the x of its frontmost unit, or its own
// castle when it has nothing on the field. This is what the camera follows and
// what the minimap marks.
float frontLineX(engine::World& world, bool leftSide);

// How many stick figures are on the field. Exposed only so a test can prove
// they are cleaned up: a unit that dies leaving its figure behind is an entity
// leak that nothing else would notice until the frame rate did.
int countFigures(engine::World& world);

// Advances every unit's walk cycle and attack swing and rebuilds its figure,
// then sweeps away figures whose owner has died. Called once per frame.
void animateUnits(engine::World& world, float dt);

// --- Upgrades (used by the game and by its tests) --------------------------

// What the next level of `upgrade` costs a side that has already bought
// `owned` of them. Geometric, so the fifth costs far more than the first.
float upgradeCost(int upgrade, int owned);

// The values a side actually plays with, once its upgrades are counted.
float goldPerSecondFor(const Session& session, bool leftSide);
int populationCapFor(const Session& session, bool leftSide);
float castleMaxHealthFor(const Session& session, bool leftSide);

// Where the upgrade buttons are, and which one is under a screen position
// (-1 for none). Exposed for the same reason `buttonAt` is: a test that clicks
// one should not have to re-derive the layout.
constexpr float kUpgradeX = 690.0f;
constexpr float kUpgradeY = 84.0f;
constexpr float kUpgradeWidth = 254.0f;
constexpr float kUpgradeHeight = 30.0f;
constexpr float kUpgradeGap = 6.0f;

constexpr float upgradeTop(int index) {
    return kUpgradeY + static_cast<float>(index) * (kUpgradeHeight + kUpgradeGap);
}

int upgradeAt(float screenX, float screenY);

// How many cannonballs are in the air. For tests; nothing else needs it.
int countCannonballs(engine::World& world);

// Which spawn-bar button is under this screen position, or -1 for none.
// Exposed because it is the rule the UI is built on, and a test should be able
// to check it without inferring it from what happened afterwards.
int buttonAt(float screenX, float screenY);

// Units of one kind on one side. `kind` of -1 counts all of them, which is
// what the population cap is measured against.
int countUnitsOfKind(engine::World& world, bool leftSide, int kind);

// Spawns one unit for a side, ignoring cost and the population cap. Exposed so
// tests can set up a fight directly instead of waiting for gold to accrue.
engine::Entity spawnUnit(engine::World& world, bool leftSide, int kind = 1);

// --- Wiring ----------------------------------------------------------------

void setAudioDevice(engine::AudioDevice* audio);

// --- Scenes ----------------------------------------------------------------

std::unique_ptr<engine::Scene> makeTitleScene();
std::unique_ptr<engine::Scene> makeStageSelectScene();

// Plays whichever stage the Campaign says is current.
std::unique_ptr<engine::Scene> makePlayScene();

// Where the stage rows are drawn, and which one is under a screen position
// (-1 for none). Exposed for the same reason the spawn bar's layout is: a test
// that clicks a stage should not have to re-derive where it sits.
constexpr float kStageX = 260.0f;
constexpr float kStageY = 120.0f;
constexpr float kStageWidth = 440.0f;
constexpr float kStageHeight = 34.0f;
constexpr float kStageGap = 6.0f;

constexpr float stageTop(int index) {
    return kStageY + static_cast<float>(index) * (kStageHeight + kStageGap);
}

int stageAt(float screenX, float screenY);

// The armoury, down the left of the stage list: one row per permanent upgrade,
// bought from the bank.
constexpr float kPerkX = 20.0f;
constexpr float kPerkY = 130.0f;
constexpr float kPerkWidth = 220.0f;
constexpr float kPerkHeight = 44.0f;
constexpr float kPerkGap = 8.0f;

constexpr float perkTop(int index) {
    return kPerkY + static_cast<float>(index) * (kPerkHeight + kPerkGap);
}

int perkAt(float screenX, float screenY);

}  // namespace lanebattle
