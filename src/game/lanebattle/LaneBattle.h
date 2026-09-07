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

constexpr float kUnitWidth = 24.0f;
constexpr float kUnitHeight = 36.0f;

// --- Tuning ----------------------------------------------------------------
//
// One unit type for now. These five numbers are the entire balance of the
// game, which is a good sign that the loop is small enough to reason about.
constexpr float kUnitHealth = 100.0f;
constexpr float kUnitDamage = 14.0f;
constexpr float kUnitRange = 34.0f;          // how close before it stops to fight
constexpr float kUnitAttackDelay = 0.6f;     // seconds between blows

// Raised from 60 when the field went from 960 pixels wide to 2400. At the old
// speed an unopposed unit needed forty seconds to cross, which is a walking
// simulator rather than a game; at this speed the two front lines meet about
// eleven seconds after a push starts, which is long enough that committing to
// one is a decision and short enough to stay interesting.
constexpr float kUnitSpeed = 95.0f;          // pixels per second

constexpr float kUnitCost = 60.0f;
constexpr float kStartingGold = 150.0f;
constexpr float kGoldPerSecond = 14.0f;
constexpr float kSpawnCooldown = 0.35f;      // stops one keypress spawning ten

// The opponent plays by exactly the same rules: the same purse, the same
// income, the same unit cost, the same cooldown. Its only "intelligence" is
// spending the moment it can afford to.
//
// It began as a free unit every three seconds, which quietly made the game
// unwinnable — the enemy out-produced the player by half again, and no amount
// of tuning the player's economy could fix a opponent that didn't have one.
// Symmetry costs nothing and makes the difficulty a single number to change.
constexpr float kEnemyIncomeMultiplier = 1.0f;

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
    float health = kUnitHealth;
    float timeUntilAttack = 0.0f;
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

    // Counts down while the player is steering the view by hand. Above zero
    // the camera obeys the arrow keys; at zero it goes back to following.
    float freeLookSeconds = 0.0f;
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

// Spawns one unit for a side, ignoring cost. Exposed so tests can set up a
// fight directly instead of waiting for gold to accrue.
engine::Entity spawnUnit(engine::World& world, bool leftSide);

// --- Wiring ----------------------------------------------------------------

void setAudioDevice(engine::AudioDevice* audio);

// --- Scenes ----------------------------------------------------------------

std::unique_ptr<engine::Scene> makeTitleScene();
std::unique_ptr<engine::Scene> makePlayScene();

}  // namespace lanebattle
