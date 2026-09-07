#pragma once
// ---------------------------------------------------------------------------
// LaneBattle.h — a castle-versus-castle lane battler, in the genre of Cartoon
// Wars and Age of War.
//
// The whole game is one loop: gold accrues, you spend it to send units right,
// they walk until they meet an enemy, they fight, and whoever's castle falls
// first loses. Unit types, upgrades and spells are all decoration on that —
// so this first slice builds only the loop, to find out whether it is any fun
// before spending anything on a camera or a mouse.
//
// Deliberately, it needs NO new engine code. Every part of it is already
// there: Transform and Velocity for walking, MovementSystem to do the walking,
// Sprite to draw, Lifetime for the death debris, destroyLater so a unit can
// die in the middle of the loop that killed it, and the scene stack for
// title/play/pause/game-over.
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

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 540;

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
constexpr float kUnitSpeed = 60.0f;          // pixels per second

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
};

// --- Queries (used by the game and by its tests) ---------------------------

Session* findSession(engine::World& world);
engine::Entity findCastle(engine::World& world, bool leftSide);
int countUnits(engine::World& world, bool leftSide);

// Spawns one unit for a side, ignoring cost. Exposed so tests can set up a
// fight directly instead of waiting for gold to accrue.
engine::Entity spawnUnit(engine::World& world, bool leftSide);

// --- Wiring ----------------------------------------------------------------

void setAudioDevice(engine::AudioDevice* audio);

// --- Scenes ----------------------------------------------------------------

std::unique_ptr<engine::Scene> makeTitleScene();
std::unique_ptr<engine::Scene> makePlayScene();

}  // namespace lanebattle
