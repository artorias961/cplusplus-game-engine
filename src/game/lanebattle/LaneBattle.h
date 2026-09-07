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
    float width;
    float height;
    unsigned char leftR, leftG, leftB;     // your colours
    unsigned char rightR, rightG, rightB;  // theirs
};

// The three roles the genre is built on, and the shape of each:
//
//   RUNNER  — cheap and fast, dies to anything, wins on gold-for-damage.
//   SOLDIER — the slice-1 unit. Beats a runner one-to-one and loses to two.
//   ARCHER  — outranges everything, but cannot survive being reached. Useless
//             alone; the reason to own a front line.
//
// These numbers are a starting point that was then measured rather than
// argued about: see the matchup probe in the tests.
constexpr UnitKind kUnitKinds[] = {
    // name       cost   hp    dmg   range  delay  speed   w      h     yours          theirs
    {"RUNNER",    35.0f, 60.0f,  8.0f,  30.0f, 0.45f, 150.0f, 16.0f, 26.0f, 150, 215, 255, 255, 175, 150},
    {"SOLDIER",   60.0f, 110.0f, 14.0f, 34.0f, 0.60f,  95.0f, 24.0f, 36.0f, 110, 190, 240, 235, 130, 110},
    {"ARCHER",    95.0f, 55.0f,  20.0f, 135.0f, 0.95f,  70.0f, 18.0f, 34.0f,  90, 140, 210, 200,  95, 130},
};
constexpr int kUnitKindCount =
    static_cast<int>(sizeof(kUnitKinds) / sizeof(kUnitKinds[0]));

// The widest and tallest of them, for the few places that need a bound before
// knowing which kind they are dealing with.
constexpr float kMaxUnitWidth = 24.0f;

// --- Tuning ----------------------------------------------------------------

constexpr float kStartingGold = 150.0f;
constexpr float kGoldPerSecond = 14.0f;
constexpr float kSpawnCooldown = 0.35f;      // stops one keypress spawning ten

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

// How many units one side may have on the field at once.
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

// The opponent's composition, cycled. It banks until it can afford the next
// whole wave and then sends it, because spending on sight is a losing policy —
// slice 2 measured that too. This is not clever, but it is no longer free to
// beat: a player who also spends on sight now loses.
constexpr int kEnemyComposition[] = {1, 1, 2, 0, 1, 0};  // soldier-heavy, some archers and runners
constexpr int kEnemyCompositionLength =
    static_cast<int>(sizeof(kEnemyComposition) / sizeof(kEnemyComposition[0]));
constexpr int kEnemyWaveSize = 3;            // units banked for before spending

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

// Dragging the field scrolls the view, which is the genre's other mouse verb.
// A few pixels of slop before a press counts as a drag, so a slightly shaky
// click on a button is still a click.
constexpr float kDragThreshold = 4.0f;

constexpr float kMinimapX = 300.0f;
constexpr float kMinimapY = 20.0f;
constexpr float kMinimapWidth = 360.0f;
constexpr float kMinimapHeight = 12.0f;
constexpr float kMinimapMarkerWidth = 6.0f;

constexpr int kFieldLayer = 0;
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

// The whole battle's state, on one entity — the singleton-component pattern
// used by the other games, which is what lets a test read the gold without
// the scene exposing anything.
struct Session {
    float gold = kStartingGold;
    float enemyGold = kStartingGold;
    bool gameOver = false;
    bool playerWon = false;

    float spawnCooldown = 0.0f;
    float enemySpawnTimer = 0.0f;

    // Where the opponent is in its composition cycle, and how much of the
    // current wave it still owes. Zero means it is banking for the next one.
    int enemyWaveIndex = 0;
    int enemyWaveRemaining = 0;

    // Counts down while the player is steering the view by hand. Above zero
    // the camera obeys the arrow keys or the drag; at zero it goes back to
    // following.
    float freeLookSeconds = 0.0f;

    // A drag in progress: where it started, and whether it has moved far
    // enough to count as one rather than as a click.
    bool dragging = false;
    float dragStartX = 0.0f;
    float dragStartCameraX = 0.0f;
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
std::unique_ptr<engine::Scene> makePlayScene();

}  // namespace lanebattle
