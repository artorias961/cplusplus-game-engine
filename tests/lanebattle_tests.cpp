// Before ANY include: LaneBattle.h reaches SDL.h through the engine headers,
// and SDL.h renames main() unless this is already defined.
#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// lanebattle_tests.cpp — the rules of the battle, checked without a window.
//
// Written alongside the first slice rather than after it, which is the one
// thing this project got wrong the last two times. There is no randomness in
// the game yet, so every test here is exactly reproducible.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdio>

#include "Harness.h"
#include "LaneBattle.h"

using namespace engine;
using lanebattle::Castle;
using lanebattle::Session;
using lanebattle::Unit;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

struct Game {
    World world;
    SceneStack scenes;
    harness::Harness driver;

    Game() : scenes(), driver(world, scenes) {}

    void startPlaying() {
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
    }

    Session& session() { return *lanebattle::findSession(world); }

    float castleHealth(bool leftSide) {
        const Entity castle = lanebattle::findCastle(world, leftSide);
        if (castle == kInvalidEntity) return -1.0f;
        return world.getComponent<Castle>(castle)->health;
    }

    // Gives a test a clear field for a controlled fight: stops the opponent
    // spending, and removes whatever it already fielded.
    //
    // That second part matters because the enemy spends the instant it can
    // afford to, so by the time setup has finished it already has a unit
    // marching. Leaving it there let a stray soldier intercept fights these
    // tests had carefully arranged.
    void suppressEnemySpawns() {
        session().enemySpawnTimer = 1.0e9f;
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Unit>(entity)) world.destroyLater(entity);
        }
        driver.step();  // let the deletions flush
    }
};

void testBattleStarts() {
    Game game;
    game.startPlaying();

    check(lanebattle::findSession(game.world) != nullptr, "a session exists");
    check(lanebattle::findCastle(game.world, true) != kInvalidEntity,
          "the player's castle exists");
    check(lanebattle::findCastle(game.world, false) != kInvalidEntity,
          "the enemy castle exists");
    check(game.castleHealth(true) == lanebattle::kCastleHealth,
          "castles start at full health");
    // Not exact equality: income starts accruing on the first frame, so a few
    // frames of setup legitimately add a little.
    check(game.session().gold >= lanebattle::kStartingGold &&
              game.session().gold < lanebattle::kStartingGold + 5.0f,
          "the purse starts full");
    check(lanebattle::countUnits(game.world, true) == 0,
          "the field starts empty");
}

void testGoldAccrues() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const float before = game.session().gold;
    game.driver.step(60);  // one second

    const float earned = game.session().gold - before;
    check(std::fabs(earned - lanebattle::kGoldPerSecond) < 1.0f,
          "roughly one second's income arrives in one second");
}

void testSpawningCostsGoldAndIsRateLimited() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const float before = game.session().gold;
    game.driver.hold(SDL_SCANCODE_A);
    game.driver.step(2);

    check(lanebattle::countUnits(game.world, true) == 1, "holding A spawns a unit");
    check(game.session().gold < before, "spawning costs gold");

    // Still held down, but the cooldown should stop a stream of units.
    game.driver.step(5);
    check(lanebattle::countUnits(game.world, true) == 1,
          "the spawn cooldown prevents one keypress becoming an army");

    game.driver.release(SDL_SCANCODE_A);
}

void testCannotSpawnWithoutGold() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    game.session().gold = 0.0f;
    game.driver.hold(SDL_SCANCODE_A);
    game.driver.step(3);
    game.driver.release(SDL_SCANCODE_A);

    check(lanebattle::countUnits(game.world, true) == 0,
          "an empty purse buys nothing");
}

void testUnitsWalkForward() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity unit = lanebattle::spawnUnit(game.world, true);
    const float startX = game.world.getComponent<Transform>(unit)->x;

    game.driver.step(30);  // half a second

    const float movedX = game.world.getComponent<Transform>(unit)->x;
    check(movedX > startX, "a left-side unit walks to the right");
    check(std::fabs((movedX - startX) - lanebattle::kUnitSpeed * 0.5f) < 2.0f,
          "it walks at about its stated speed");
}

void testUnitsStopAndFight() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    // Put two enemies within reach of each other, mid-field.
    const Entity mine = lanebattle::spawnUnit(game.world, true);
    const Entity theirs = lanebattle::spawnUnit(game.world, false);
    game.world.getComponent<Transform>(mine)->x = 400.0f;
    game.world.getComponent<Transform>(theirs)->x =
        400.0f + lanebattle::kUnitRange - 4.0f;

    game.driver.step(2);

    check(std::fabs(game.world.getComponent<Velocity>(mine)->dx) < 0.01f,
          "a unit within reach of an enemy stops walking");
    check(game.world.getComponent<Unit>(theirs)->health < lanebattle::kUnitHealth,
          "and starts hitting it");
}

void testLosingUnitsDie() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity mine = lanebattle::spawnUnit(game.world, true);
    const Entity theirs = lanebattle::spawnUnit(game.world, false);
    game.world.getComponent<Transform>(mine)->x = 400.0f;
    game.world.getComponent<Transform>(theirs)->x =
        400.0f + lanebattle::kUnitRange - 4.0f;

    // Leave the enemy on its last legs; the next blow should finish it.
    game.world.getComponent<Unit>(theirs)->health = 1.0f;
    game.driver.step(5);

    check(lanebattle::countUnits(game.world, false) == 0,
          "a unit reduced to zero health is removed");
    check(lanebattle::countUnits(game.world, true) == 1,
          "the survivor is left standing");

    // And having killed it, the survivor should march on rather than stand
    // there swinging at nothing.
    game.driver.step(10);
    check(game.world.getComponent<Velocity>(mine)->dx > 0.0f,
          "the survivor resumes walking once its target is gone");
}

void testUnitsDamageTheEnemyCastle() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity mine = lanebattle::spawnUnit(game.world, true);
    const Entity enemyCastle = lanebattle::findCastle(game.world, false);
    const float castleX = game.world.getComponent<Transform>(enemyCastle)->x;

    // Walk it up to the castle wall.
    game.world.getComponent<Transform>(mine)->x =
        castleX - lanebattle::kUnitRange + 4.0f;

    const float before = game.castleHealth(false);
    game.driver.step(3);

    check(game.castleHealth(false) < before,
          "a unit in reach of the enemy castle damages it");
}

void testBreakingTheCastleWinsTheBattle() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity mine = lanebattle::spawnUnit(game.world, true);
    const Entity enemyCastle = lanebattle::findCastle(game.world, false);
    game.world.getComponent<Transform>(mine)->x =
        game.world.getComponent<Transform>(enemyCastle)->x -
        lanebattle::kUnitRange + 4.0f;
    game.world.getComponent<Castle>(enemyCastle)->health = 1.0f;

    game.driver.step(4);

    check(game.session().gameOver, "the battle ends when a castle falls");
    check(game.session().playerWon, "and breaking the ENEMY castle is a win");
}

void testLosingYourOwnCastle() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity theirs = lanebattle::spawnUnit(game.world, false);
    const Entity myCastle = lanebattle::findCastle(game.world, true);
    game.world.getComponent<Transform>(theirs)->x =
        game.world.getComponent<Transform>(myCastle)->x +
        lanebattle::kCastleWidth + 20.0f;  // just outside the castle wall
    game.world.getComponent<Castle>(myCastle)->health = 1.0f;

    game.driver.step(4);

    check(game.session().gameOver, "losing your castle ends the battle");
    check(!game.session().playerWon, "and it is not a win");
}

void testTheEnemySpawnsOnItsOwn() {
    Game game;
    game.startPlaying();

    game.driver.step(5);
    check(lanebattle::countUnits(game.world, false) >= 1,
          "the enemy sends units of its own");
}

// The enemy used to spawn free while the player paid, which made the game
// quietly unwinnable: it out-produced the player by half again, and no tuning
// of the player's economy could fix an opponent that had no economy at all.
void testTheEnemyPaysForItsUnits() {
    Game game;
    game.startPlaying();

    // It has already spent once during setup, so hand it a fresh purse and an
    // open cooldown and watch it spend again.
    game.session().enemyGold = lanebattle::kUnitCost + 10.0f;
    game.session().enemySpawnTimer = 0.0f;

    const int fieldedBefore = lanebattle::countUnits(game.world, false);
    const float before = game.session().enemyGold;
    game.driver.step(2);

    check(lanebattle::countUnits(game.world, false) == fieldedBefore + 1,
          "the enemy spawned");
    check(game.session().enemyGold < before, "and paid for it");

    // Broke means idle, exactly as it does for the player.
    game.session().enemyGold = 0.0f;
    const int fielded = lanebattle::countUnits(game.world, false);
    game.driver.step(4);
    check(lanebattle::countUnits(game.world, false) == fielded,
          "an empty enemy purse buys nothing either");
}

// Both sides earn at the same rate, so neither can out-produce the other by
// construction. Difficulty is kEnemyIncomeMultiplier and nothing else.
void testTheEconomiesAreSymmetric() {
    Game game;
    game.startPlaying();

    // Nobody spends: hold the enemy's cooldown open and press nothing.
    game.session().enemySpawnTimer = 1.0e9f;
    const float playerBefore = game.session().gold;
    const float enemyBefore = game.session().enemyGold;

    game.driver.step(120);  // two seconds

    const float playerEarned = game.session().gold - playerBefore;
    const float enemyEarned = game.session().enemyGold - enemyBefore;
    check(std::fabs(playerEarned - enemyEarned) < 0.01f,
          "both sides earn at the same rate");
}

void testPauseFreezesTheBattle() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity unit = lanebattle::spawnUnit(game.world, true);
    game.driver.step(5);

    game.driver.tap(SDL_SCANCODE_P);
    game.driver.step(2);

    const float frozenX = game.world.getComponent<Transform>(unit)->x;
    const float frozenGold = game.session().gold;
    game.driver.step(30);

    check(std::fabs(game.world.getComponent<Transform>(unit)->x - frozenX) < 0.01f,
          "units do not move while paused");
    check(game.session().gold == frozenGold, "and gold does not accrue");

    game.driver.tap(SDL_SCANCODE_P);
    game.driver.step(5);
    check(game.world.getComponent<Transform>(unit)->x > frozenX,
          "unpausing resumes the battle");
}

void testRestartingAfterDefeat() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    // End it quickly.
    const Entity mine = lanebattle::spawnUnit(game.world, true);
    const Entity enemyCastle = lanebattle::findCastle(game.world, false);
    game.world.getComponent<Transform>(mine)->x =
        game.world.getComponent<Transform>(enemyCastle)->x -
        lanebattle::kUnitRange + 4.0f;
    game.world.getComponent<Castle>(enemyCastle)->health = 1.0f;
    game.driver.step(4);
    check(game.session().gameOver, "the battle is over");

    game.driver.tap(SDL_SCANCODE_R);
    game.driver.step(3);

    check(!game.session().gameOver, "R starts a fresh battle");
    check(game.session().gold >= lanebattle::kStartingGold &&
              game.session().gold < lanebattle::kStartingGold + 5.0f,
          "the purse resets");
    check(game.castleHealth(false) == lanebattle::kCastleHealth,
          "the enemy castle is rebuilt");
    check(lanebattle::countUnits(game.world, true) == 0,
          "the previous battle's units are gone");
    // The enemy starts spending again immediately with its refilled purse, so
    // it may already have one on the field. What matters is that the old army
    // didn't survive the reset.
    check(lanebattle::countUnits(game.world, false) <= 1,
          "and the enemy is starting over too");
}

}  // namespace

int main() {
    std::printf("lane battle tests\n");

    testBattleStarts();
    testGoldAccrues();
    testSpawningCostsGoldAndIsRateLimited();
    testCannotSpawnWithoutGold();
    testUnitsWalkForward();
    testUnitsStopAndFight();
    testLosingUnitsDie();
    testUnitsDamageTheEnemyCastle();
    testBreakingTheCastleWinsTheBattle();
    testLosingYourOwnCastle();
    testTheEnemySpawnsOnItsOwn();
    testTheEnemyPaysForItsUnits();
    testTheEconomiesAreSymmetric();
    testPauseFreezesTheBattle();
    testRestartingAfterDefeat();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
