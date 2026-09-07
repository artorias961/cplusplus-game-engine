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

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <fstream>
#include <string>

#include "Harness.h"
#include "LaneBattle.h"
#include "engine/View.h"

using namespace engine;
using lanebattle::Castle;
using lanebattle::Session;
using lanebattle::Unit;

namespace {

// Names for the rows of the roster, so the tests read as prose rather than as
// indices. `stats(kSoldier).range` says what it means; `kUnitKinds[1].range`
// does not.
constexpr int kRunner = 0;
constexpr int kSoldier = 1;
constexpr int kArcher = 2;

const lanebattle::UnitKind& stats(int kind) {
    return lanebattle::unitKind(kind);
}

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

    // The roster is global mutable state now that a file can change it, so
    // every case starts from the compiled-in defaults. Without this, one test
    // loading a file would quietly change the meaning of every test after it —
    // and the failure would land somewhere else entirely.
    Game() : scenes(), driver(world, scenes) { lanebattle::resetBalance(); }

    void startPlaying() {
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
    }

    Session& session() { return *lanebattle::findSession(world); }

    float cameraX() {
        Camera* camera = lanebattle::findCamera(world);
        return camera ? camera->x : -1.0f;
    }

    float castleHealth(bool leftSide) {
        const Entity castle = lanebattle::findCastle(world, leftSide);
        if (castle == kInvalidEntity) return -1.0f;
        return world.getComponent<Castle>(castle)->health;
    }

    // Gives a test a clear field for a controlled fight: stops the opponent
    // spending, and removes whatever it already fielded.
    //
    // That second part matters because the enemy will send a wave as soon as
    // it has banked for one, so a test that runs for a few seconds finds
    // strangers wandering into the fight it carefully arranged.
    void suppressEnemySpawns() {
        session().enemySpawnTimer = 1.0e9f;
        // And its cannon. A test that walks units up the field for long enough
        // used to be safe; now anything that strays within range of the enemy
        // castle gets shelled, which is correct behaviour and a nuisance in a
        // test that was arranging something else.
        session().enemyCannonCooldown = 1.0e9f;
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
    game.driver.hold(SDL_SCANCODE_2);
    game.driver.step(2);

    check(lanebattle::countUnits(game.world, true) == 1, "holding A spawns a unit");
    check(game.session().gold < before, "spawning costs gold");

    // Still held down, but the cooldown should stop a stream of units.
    game.driver.step(5);
    check(lanebattle::countUnits(game.world, true) == 1,
          "the spawn cooldown prevents one keypress becoming an army");

    game.driver.release(SDL_SCANCODE_2);
}

void testCannotSpawnWithoutGold() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    game.session().gold = 0.0f;
    game.driver.hold(SDL_SCANCODE_2);
    game.driver.step(3);
    game.driver.release(SDL_SCANCODE_2);

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
    check(std::fabs((movedX - startX) - stats(kSoldier).speed * 0.5f) < 2.0f,
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
        400.0f + stats(kSoldier).range - 4.0f;

    game.driver.step(2);

    check(std::fabs(game.world.getComponent<Velocity>(mine)->dx) < 0.01f,
          "a unit within reach of an enemy stops walking");
    check(game.world.getComponent<Unit>(theirs)->health < stats(kSoldier).health,
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
        400.0f + stats(kSoldier).range - 4.0f;

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
        castleX - stats(kSoldier).range + 4.0f;

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
        stats(kSoldier).range + 4.0f;
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

    // It banks for a whole wave first, so nothing appears immediately — which
    // is the point of the change, and worth asserting rather than assuming.
    game.driver.step(5);
    check(lanebattle::countUnits(game.world, false) == 0,
          "the enemy does not spend on sight any more");

    game.driver.step(60 * 12);  // long enough to bank and send
    check(lanebattle::countUnits(game.world, false) >= 2,
          "it sends a wave rather than a trickle");
}

// The opponent used to spend the instant it could afford to, which measuring a
// whole battle showed to be a losing policy: a player doing the same thing
// mirrors it exactly and the front line never moves. Now it banks, so beating
// it takes something other than copying it.
void testTheEnemyBanksBeforeSpending() {
    Game game;
    game.startPlaying();

    // Enough for one unit but not for a wave: it should sit on the money.
    game.session().enemyGold = stats(kSoldier).cost + 5.0f;
    game.session().enemyWaveRemaining = 0;
    game.session().enemySpawnTimer = 0.0f;

    const int before = lanebattle::countUnits(game.world, false);
    game.driver.step(3);
    check(lanebattle::countUnits(game.world, false) == before,
          "one unit's worth of gold does not buy one unit");

    // Enough for a whole wave: now it spends.
    game.session().enemyGold = 10000.0f;
    game.driver.step(60);
    check(lanebattle::countUnits(game.world, false) >= before + 2,
          "a full purse buys a wave");
}

// Its composition cycles rather than being one unit type repeated, so the
// player faces a mix and cannot answer everything with one counter.
void testTheEnemyMixesItsWave() {
    Game game;
    game.startPlaying();

    game.session().enemyGold = 10000.0f;
    game.driver.step(60 * 8);

    int kindsSeen = 0;
    for (int kind = 0; kind < lanebattle::unitKindCount(); ++kind) {
        if (lanebattle::countUnitsOfKind(game.world, false, kind) > 0) ++kindsSeen;
    }
    check(kindsSeen >= 2, "the enemy fields more than one kind of unit");
}

// The enemy used to spawn free while the player paid, which made the game
// quietly unwinnable: it out-produced the player by half again, and no tuning
// of the player's economy could fix an opponent that had no economy at all.
void testTheEnemyPaysForItsUnits() {
    Game game;
    game.startPlaying();

    // Hand it a purse big enough for a wave and an open cooldown, then watch
    // the money actually leave the purse.
    game.session().enemyGold = 10000.0f;
    game.session().enemySpawnTimer = 0.0f;

    const int fieldedBefore = lanebattle::countUnits(game.world, false);
    const float before = game.session().enemyGold;
    game.driver.step(2);

    check(lanebattle::countUnits(game.world, false) == fieldedBefore + 1,
          "the enemy spawned");
    check(game.session().enemyGold < before, "and paid for it");

    // Broke means idle, exactly as it does for the player.
    game.session().enemyGold = 0.0f;
    game.session().enemyWaveRemaining = 0;
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
        stats(kSoldier).range + 4.0f;
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

// --- The view (slice 2) ----------------------------------------------------
//
// The battlefield is now two and a half screens wide, so where the camera is
// pointing decides what the player can actually see — which makes it game
// state worth testing, not presentation. None of this needs a window: the
// camera is a component, so a headless run can read it directly.

void testTheFieldIsWiderThanTheWindow() {
    Game game;
    game.startPlaying();

    const Entity leftCastle = lanebattle::findCastle(game.world, true);
    const Entity rightCastle = lanebattle::findCastle(game.world, false);
    const float leftX = game.world.getComponent<Transform>(leftCastle)->x;
    const float rightX = game.world.getComponent<Transform>(rightCastle)->x;

    check(lanebattle::kWorldWidth > static_cast<float>(lanebattle::kWindowWidth),
          "the world is wider than the view");
    check(rightX > static_cast<float>(lanebattle::kWindowWidth),
          "the enemy castle starts off-screen");
    check(rightX - leftX > static_cast<float>(lanebattle::kWindowWidth),
          "and the two castles are more than a screen apart");
}

void testTheCameraStartsOnYourOwnCastle() {
    Game game;
    game.startPlaying();

    check(lanebattle::findCamera(game.world) != nullptr, "there is a camera");

    // Your castle is near the left edge, so centring on it would need a
    // negative camera position; clamping pins the view to the world edge
    // instead. Starting anywhere else would mean the first frame lurches.
    check(game.cameraX() == 0.0f,
          "the view starts pinned to the left edge of the world");
}

void testTheCameraFollowsYourFrontLine() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity mine = lanebattle::spawnUnit(game.world, true);
    game.world.getComponent<Transform>(mine)->x = 1200.0f;

    const float before = game.cameraX();
    game.driver.step(120);  // two seconds of catching up
    const float after = game.cameraX();

    check(after > before, "the view moves towards an advancing unit");

    // Measured against where the unit ACTUALLY is, not where it was put: it
    // is still marching while the camera chases it, so it has moved a couple
    // of hundred pixels by now. A tolerance wide enough to cover that would
    // have stopped testing anything.
    //
    // The camera trails a moving target by about speed/followRate — roughly
    // 24 pixels here — because easing never quite catches something that
    // keeps running away. That is a feature; it is what stops the view
    // snapping.
    const float unitX = game.world.getComponent<Transform>(mine)->x;
    const float wanted = unitX - static_cast<float>(lanebattle::kWindowWidth) / 2.0f;
    check(std::fabs(after - wanted) < 40.0f,
          "and settles with that unit near the middle of the screen");
}

void testTheCameraStopsAtTheEdgesOfTheWorld() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    // A unit at the far end: centring on it would run the view off the end of
    // the world, so it should stop at the last full screen instead.
    const Entity mine = lanebattle::spawnUnit(game.world, true);
    game.world.getComponent<Transform>(mine)->x = lanebattle::kWorldWidth - 10.0f;
    game.driver.step(300);

    check(game.cameraX() <= lanebattle::kCameraMaxX + 0.01f,
          "the view never scrolls past the right edge of the world");
    check(game.cameraX() > lanebattle::kCameraMaxX - 1.0f,
          "but does reach it");

    // And back the other way, with nothing on the field at all.
    game.world.destroyLater(mine);
    game.driver.step(300);
    check(game.cameraX() >= 0.0f,
          "and never scrolls past the left edge either");
    check(game.cameraX() < 1.0f, "returning home when the field is empty");
}

void testArrowKeysTakeTheViewOffTheLeash() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const float before = game.cameraX();

    game.driver.hold(SDL_SCANCODE_RIGHT);
    game.driver.step(30);
    game.driver.release(SDL_SCANCODE_RIGHT);

    const float scouted = game.cameraX();
    check(scouted > before + 100.0f,
          "holding RIGHT scrolls the view away from the front line");

    // Letting go does not snap it back instantly — you get a moment to look.
    game.driver.step(2);
    check(std::fabs(game.cameraX() - scouted) < 5.0f,
          "and letting go holds the view briefly rather than snapping back");

    // But it does hand control back eventually.
    game.driver.step(240);  // four seconds, well past the hold
    check(game.cameraX() < scouted - 50.0f,
          "then following resumes and the view comes home");
}

// Following clamps its target before it ever moves the camera, so the edge
// test above passes even if the camera's own clamp is deleted. Free-look is
// the path that can genuinely run off the end of the world, and it needs
// checking separately — steering is exactly where a view escapes.
void testFreeLookRespectsTheWorldEdges() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    game.driver.hold(SDL_SCANCODE_RIGHT);
    game.driver.step(300);  // five seconds: far more than the field is wide
    check(game.cameraX() <= lanebattle::kCameraMaxX + 0.01f,
          "steering right stops at the edge of the world");
    check(game.cameraX() > lanebattle::kCameraMaxX - 1.0f, "having reached it");
    game.driver.release(SDL_SCANCODE_RIGHT);

    game.driver.hold(SDL_SCANCODE_LEFT);
    game.driver.step(300);
    check(game.cameraX() >= 0.0f, "and steering left stops at the other edge");
    check(game.cameraX() < 1.0f, "having reached that one too");
    game.driver.release(SDL_SCANCODE_LEFT);
}

// The HUD is drawn in screen space, which did nothing at all while every world
// fit inside its window. On a scrolling field it is the difference between a
// score in the corner and a score that slides off the side of the screen, so
// it is worth pinning down that the game actually asks for it.
void testTheHudIgnoresTheCamera() {
    Game game;
    game.startPlaying();

    int worldSpaceText = 0;
    int screenSpaceText = 0;
    for (Entity entity : game.world.entities()) {
        if (Text* text = game.world.getComponent<Text>(entity)) {
            if (text->screenSpace) {
                ++screenSpaceText;
            } else {
                ++worldSpaceText;
            }
        }
    }

    check(screenSpaceText > 0, "the HUD exists");
    check(worldSpaceText == 0, "and every word of it ignores the camera");
}

// The minimap is the only way to see a push that is happening off-screen, so
// its markers have to actually track the battle rather than sit still.
void testTheMinimapTracksTheFrontLines() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    check(std::fabs(lanebattle::frontLineX(game.world, true) -
                    (lanebattle::kCastleMargin + lanebattle::kCastleWidth / 2.0f)) <
              1.0f,
          "with nothing fielded, your front line is your own castle");

    const Entity mine = lanebattle::spawnUnit(game.world, true);
    game.world.getComponent<Transform>(mine)->x = 900.0f;

    const Entity behind = lanebattle::spawnUnit(game.world, true);
    game.world.getComponent<Transform>(behind)->x = 300.0f;
    game.driver.step();

    check(std::fabs(lanebattle::frontLineX(game.world, true) - 900.0f) < 5.0f,
          "the front line is the furthest-advanced unit, not the newest");

    // The right side advances the other way, so "furthest forward" for them is
    // the SMALLEST x — the one comparison most likely to be written backwards.
    const Entity theirs = lanebattle::spawnUnit(game.world, false);
    game.world.getComponent<Transform>(theirs)->x = 1500.0f;
    const Entity theirsBehind = lanebattle::spawnUnit(game.world, false);
    game.world.getComponent<Transform>(theirsBehind)->x = 2000.0f;
    game.driver.step();

    check(std::fabs(lanebattle::frontLineX(game.world, false) - 1500.0f) < 5.0f,
          "and for the enemy, forward is the other way");
}

// --- The roster (slice 3) --------------------------------------------------

void testEachKeySendsItsOwnKind() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10000.0f;

    const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3};
    for (int kind = 0; kind < 3; ++kind) {
        game.driver.hold(keys[kind]);
        game.driver.step(2);
        game.driver.release(keys[kind]);
        game.driver.step(30);  // let the cooldown clear
        check(lanebattle::countUnitsOfKind(game.world, true, kind) == 1,
              "each key sends its own kind of unit");
    }
}

void testKindsCostWhatTheTableSays() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3};
    for (int kind = 0; kind < 3; ++kind) {
        game.session().gold = 10000.0f;
        const float before = game.session().gold;

        game.driver.hold(keys[kind]);
        game.driver.step(2);
        game.driver.release(keys[kind]);

        // Income accrues over those two frames, so this is not exact.
        const float spent = before - game.session().gold;
        check(std::fabs(spent - stats(kind).cost) < 2.0f,
              "a unit costs what its row of the table says");
        game.driver.step(30);
    }
}

// The cap is the reason "send more" stops being the answer to everything.
// Without it, slice 2 measured banking six units as barely better than banking
// two — there was nothing to think about after the first decision.
void testThePopulationCapHolds() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 100000.0f;

    // Long enough to fill the cap several times over at one spawn per
    // cooldown, but not so long that the first runners cross the field and
    // start a fight at the far castle — which would end the battle and stop
    // the spawning this is trying to measure.
    game.driver.hold(SDL_SCANCODE_1);
    game.driver.step(400);
    game.driver.release(SDL_SCANCODE_1);

    check(lanebattle::countUnits(game.world, true) <= lanebattle::kPopulationCap,
          "you can never field more than the cap");
    check(lanebattle::countUnits(game.world, true) == lanebattle::kPopulationCap,
          "and with money and time you reach it");

    // A slot freed by a death is a slot you get back.
    for (Entity entity : game.world.entities()) {
        if (Unit* unit = game.world.getComponent<Unit>(entity)) {
            unit->health = 0.0f;
            break;
        }
    }
    game.driver.step(4);
    check(lanebattle::countUnits(game.world, true) < lanebattle::kPopulationCap,
          "a death frees a slot");
}

// Units queue instead of standing inside one another. Before this, an entire
// army occupied a single pixel and fought as one enormous unit, which is
// exactly what made massing unconditionally correct.
void testUnitsHoldRank() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    // One soldier stopped at the enemy castle, another walking into its back.
    const Entity front = lanebattle::spawnUnit(game.world, true, kSoldier);
    const Entity behind = lanebattle::spawnUnit(game.world, true, kSoldier);
    const Entity castle = lanebattle::findCastle(game.world, false);
    const float castleX = game.world.getComponent<Transform>(castle)->x;

    game.world.getComponent<Transform>(front)->x =
        castleX - stats(kSoldier).range;
    game.world.getComponent<Transform>(behind)->x =
        castleX - stats(kSoldier).range - 200.0f;

    game.driver.step(60 * 5);

    const float frontX = game.world.getComponent<Transform>(front)->x;
    const float behindX = game.world.getComponent<Transform>(behind)->x;
    const float gap = frontX - behindX - stats(kSoldier).width;

    check(behindX < frontX, "the second unit stays behind the first");
    check(gap > lanebattle::kRankGap - 2.0f,
          "and keeps a rank's worth of clear space rather than standing in it");
}

// The one comparison that makes ranged units support rather than a trap: a
// friendly only blocks you if its reach is no longer than yours. Without it,
// the first archer sent walls in every melee unit behind it.
void testMeleeWalksPastItsOwnArchers() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity archer = lanebattle::spawnUnit(game.world, true, kArcher);
    const Entity soldier = lanebattle::spawnUnit(game.world, true, kSoldier);
    const Entity castle = lanebattle::findCastle(game.world, false);
    const float castleX = game.world.getComponent<Transform>(castle)->x;

    // Archer already parked at its standoff, soldier coming up behind it.
    game.world.getComponent<Transform>(archer)->x =
        castleX - stats(kArcher).range;
    game.world.getComponent<Transform>(soldier)->x =
        castleX - stats(kArcher).range - 120.0f;

    game.driver.step(60 * 6);

    const float archerX = game.world.getComponent<Transform>(archer)->x;
    const float soldierX = game.world.getComponent<Transform>(soldier)->x;

    check(soldierX > archerX + 60.0f,
          "a soldier walks past its own archers to reach the enemy");
    check(std::fabs(game.world.getComponent<Velocity>(soldier)->dx) < 0.01f,
          "and stops because the castle is in reach, not because a friend is");
}

// Two identical enemies at exactly the same distance: whichever is chosen, the
// choice must not depend on hash order. It would differ between standard
// libraries, so the same battle could play out differently on Linux than on
// Windows — a bug that arrives on a platform this machine cannot run, in a
// test that passes here every time.
// Checking that repeated runs agree proves nothing: on any one machine the
// hash order is already stable, so a run-to-run test passes with or without
// the rule. What is actually guaranteed — and what differs between standard
// libraries if it is not — is WHICH of the two is picked. So this asserts the
// rule itself: the lower entity id wins. Padding shifts the ids between
// attempts so the pair is not the same two numbers each time.
void testTargetTiesGoToTheLowerEntityId() {
    for (int padding = 0; padding < 5; ++padding) {
        Game game;
        game.startPlaying();
        game.suppressEnemySpawns();

        for (int pad = 0; pad < padding; ++pad) game.world.createEntity();

        const Entity attacker = lanebattle::spawnUnit(game.world, true, kSoldier);
        const Entity first = lanebattle::spawnUnit(game.world, false, kSoldier);
        const Entity second = lanebattle::spawnUnit(game.world, false, kSoldier);

        game.world.getComponent<Transform>(attacker)->x = 500.0f;
        game.world.getComponent<Transform>(first)->x = 520.0f;
        game.world.getComponent<Transform>(second)->x = 520.0f;
        game.driver.step(2);

        const Entity lower = std::min(first, second);
        const Entity higher = std::max(first, second);
        check(game.world.getComponent<Unit>(lower)->health < stats(kSoldier).health,
              "an exact tie is decided in favour of the lower entity id");
        check(std::fabs(game.world.getComponent<Unit>(higher)->health -
                        stats(kSoldier).health) < 0.01f,
              "and the other one is left alone");
    }
}

void testArchersOutrangeSoldiers() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity archer = lanebattle::spawnUnit(game.world, true, kArcher);
    const Entity victim = lanebattle::spawnUnit(game.world, false, kSoldier);

    // Placed inside the archer's reach but well outside the soldier's.
    game.world.getComponent<Transform>(archer)->x = 400.0f;
    game.world.getComponent<Transform>(victim)->x =
        400.0f + stats(kArcher).range - 10.0f;

    game.driver.step(2);

    check(game.world.getComponent<Unit>(victim)->health < stats(kSoldier).health,
          "an archer hits from beyond a soldier's reach");
    check(std::fabs(game.world.getComponent<Unit>(archer)->health -
                    stats(kArcher).health) < 0.01f,
          "and takes nothing back while it does");
}

// Income alone cannot break a symmetry: two competent sides earn identically
// no matter what happens on the field, so a won fight buys nothing. Paying for
// kills is what turns an advantage on the ground into an advantage in the
// purse. Without it, measurement showed every good strategy drawing and every
// bad one losing, with nothing in between.
void testKillsPayGold() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity mine = lanebattle::spawnUnit(game.world, true, kSoldier);
    const Entity theirs = lanebattle::spawnUnit(game.world, false, kArcher);
    game.world.getComponent<Transform>(mine)->x = 400.0f;
    game.world.getComponent<Transform>(theirs)->x =
        400.0f + stats(kSoldier).range - 4.0f;
    game.world.getComponent<Unit>(theirs)->health = 1.0f;

    const float before = game.session().gold;
    game.driver.step(5);

    check(lanebattle::countUnits(game.world, false) == 0, "the archer died");

    // Income accrues over those frames too, so this checks the bounty arrived
    // rather than the exact total.
    const float expected = stats(kArcher).cost * lanebattle::kKillRewardFraction;
    const float gained = game.session().gold - before;
    check(gained > expected * 0.9f, "killing it paid a bounty");
    check(std::fabs(gained - expected) < 5.0f,
          "and the bounty is a share of what the casualty cost");

    // The bounty goes to the killer, not to whoever owned the casualty.
    const float enemyBefore = game.session().enemyGold;
    game.world.getComponent<Unit>(mine)->health = 0.0f;
    game.driver.step(3);
    check(game.session().enemyGold - enemyBefore >
              stats(kSoldier).cost * lanebattle::kKillRewardFraction * 0.9f,
          "and it is the other side that gets paid when you lose one");
}

// --- The spawn bar (slice 4) -----------------------------------------------
//
// The buttons are screen-space, so they are hit-tested straight against the
// cursor with no camera involved — which is why none of this uses
// screenToWorld. That inverse exists for clicking on the *field*, which
// nothing does yet.

// The centre of button `index`, which is where a test clicks.
float buttonCenterX(int index) {
    return lanebattle::buttonLeft(index) + lanebattle::kButtonWidth / 2.0f;
}
float buttonCenterY() {
    return lanebattle::kButtonY + lanebattle::kButtonHeight / 2.0f;
}

void testButtonHitTesting() {
    for (int kind = 0; kind < lanebattle::unitKindCount(); ++kind) {
        check(lanebattle::buttonAt(buttonCenterX(kind), buttonCenterY()) == kind,
              "the centre of a button is that button");
    }

    check(lanebattle::buttonAt(buttonCenterX(0), buttonCenterY() - 200.0f) == -1,
          "the battlefield above the bar is not a button");
    check(lanebattle::buttonAt(lanebattle::buttonLeft(0) - 8.0f,
                               buttonCenterY()) == -1,
          "nor is the margin to the left of the first one");

    // The gap between two buttons belongs to neither, or a sloppy click sends
    // whichever one the arithmetic happened to round towards.
    const float betweenX = lanebattle::buttonLeft(0) + lanebattle::kButtonWidth +
                           lanebattle::kButtonGap / 2.0f;
    check(lanebattle::buttonAt(betweenX, buttonCenterY()) == -1,
          "and the gap between two buttons is neither of them");
}

void testClickingAButtonSendsItsUnit() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10000.0f;

    for (int kind = 0; kind < lanebattle::unitKindCount(); ++kind) {
        game.driver.clickAt(static_cast<int>(buttonCenterX(kind)),
                            static_cast<int>(buttonCenterY()));
        game.driver.step(2);
        check(lanebattle::countUnitsOfKind(game.world, true, kind) == 1,
              "clicking a button sends that button's unit");
        game.driver.step(30);  // clear the shared cooldown
    }
}

// One physical click buys one unit. Without the pressed-this-frame edge, a
// button held down for a third of a second empties the purse.
void testHoldingTheMouseDoesNotEmptyThePurse() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10000.0f;

    game.driver.moveMouse(static_cast<int>(buttonCenterX(kSoldier)),
                          static_cast<int>(buttonCenterY()));
    game.driver.pressMouse();
    game.driver.step(120);  // two seconds of holding it down
    game.driver.releaseMouse();
    game.driver.step();

    check(lanebattle::countUnits(game.world, true) == 1,
          "holding the button down buys exactly one unit");
}

void testClickingTheFieldBuysNothing() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10000.0f;

    game.driver.clickAt(480, 200);  // mid-field, well above the bar
    game.driver.step(4);

    check(lanebattle::countUnits(game.world, true) == 0,
          "clicking the battlefield does not send anything");
}

void testAnUnaffordableButtonDoesNothing() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = stats(kArcher).cost - 10.0f;

    game.driver.clickAt(static_cast<int>(buttonCenterX(kArcher)),
                        static_cast<int>(buttonCenterY()));
    game.driver.step(4);

    check(lanebattle::countUnitsOfKind(game.world, true, kArcher) == 0,
          "a button you cannot afford sends nothing");
    check(game.session().gold > 0.0f, "and takes no money");
}

void testDraggingScrollsTheView() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    // Somewhere on the field, not on the bar.
    game.driver.moveMouse(600, 200);
    game.driver.pressMouse();
    game.driver.step();

    const float before = game.cameraX();
    game.driver.moveMouse(300, 200);  // dragged 300px to the left
    game.driver.step();

    check(game.cameraX() > before + 200.0f,
          "dragging the field left scrolls the view right");
    check(std::fabs((game.cameraX() - before) - 300.0f) < 1.0f,
          "by as far as the cursor moved");

    game.driver.releaseMouse();
    game.driver.step();

    // And it obeys the same limits following does.
    game.driver.moveMouse(600, 200);
    game.driver.pressMouse();
    game.driver.step();
    game.driver.moveMouse(-100000, 200);
    game.driver.step();
    check(game.cameraX() <= lanebattle::kCameraMaxX + 0.01f,
          "a drag cannot leave the world either");
    game.driver.releaseMouse();
}

// A press that starts on the bar belongs to the button under it. Without this
// every click on a button would also shove the camera a few pixels.
void testDraggingFromTheBarDoesNotScroll() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10000.0f;

    game.driver.moveMouse(static_cast<int>(buttonCenterX(kSoldier)),
                          static_cast<int>(buttonCenterY()));
    game.driver.pressMouse();
    game.driver.step();

    const float before = game.cameraX();
    game.driver.moveMouse(static_cast<int>(buttonCenterX(kSoldier)) - 250,
                          static_cast<int>(buttonCenterY()));
    game.driver.step();

    check(std::fabs(game.cameraX() - before) < 1.0f,
          "a press that started on the spawn bar never becomes a drag");
    game.driver.releaseMouse();
}

// The keys are not a legacy path to be quietly dropped: they are faster than
// the bar once you know the roster, and both routes must spend gold the same
// way or they will drift apart.
void testTheKeysStillWork() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10000.0f;

    const float before = game.session().gold;
    game.driver.tap(SDL_SCANCODE_3);
    game.driver.step(2);

    check(lanebattle::countUnitsOfKind(game.world, true, kArcher) == 1,
          "the number keys still send units");
    check(std::fabs((before - game.session().gold) - stats(kArcher).cost) < 2.0f,
          "and charge the same as clicking does");
}

// --- Animation (slice 5) ---------------------------------------------------
//
// A unit is now two entities: a coloured block and a stick figure drawn over
// it. None of what that looks like can be tested here — no window, no eyes —
// so these check the two things that CAN go wrong invisibly: the figure
// tracking the wrong thing, and figures piling up after their units die.

Entity figureOf(World& world, Entity owner) {
    for (Entity entity : world.entities()) {
        lanebattle::Figure* figure = world.getComponent<lanebattle::Figure>(entity);
        if (figure && figure->owner == owner) return entity;
    }
    return kInvalidEntity;
}

void testEveryUnitHasAFigure() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    check(lanebattle::countFigures(game.world) == 0, "an empty field has none");

    lanebattle::spawnUnit(game.world, true, kSoldier);
    lanebattle::spawnUnit(game.world, true, kArcher);
    game.driver.step();

    check(lanebattle::countFigures(game.world) == 2,
          "one figure per unit, whatever kind");
}

void testAFigureFollowsItsUnit() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity unit = lanebattle::spawnUnit(game.world, true, kSoldier);
    game.driver.step();

    const Entity figure = figureOf(game.world, unit);
    check(figure != kInvalidEntity, "the figure can be found from its unit");

    game.driver.step(30);  // half a second of walking

    const Transform* body = game.world.getComponent<Transform>(unit);
    const Transform* drawn = game.world.getComponent<Transform>(figure);
    check(std::fabs(drawn->x - (body->x + stats(kSoldier).width / 2.0f)) < 0.01f,
          "it sits on the centre line of the block it decorates");
    check(std::fabs(drawn->y - lanebattle::kGroundY) < 0.01f,
          "with its feet on the ground");

    const Polygon* lines = game.world.getComponent<Polygon>(figure);
    check(lines != nullptr && lines->points.size() >= 4,
          "and it actually has limbs to draw");
}

// The walk cycle is driven by distance travelled rather than by time, so a
// runner's legs move faster than a soldier's without either being told to, and
// nothing ever slides along with its feet still.
void testLegsMoveWithTheUnitNotWithTheClock() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity walker = lanebattle::spawnUnit(game.world, true, kSoldier);
    game.driver.step(30);
    const float walked = game.world.getComponent<Unit>(walker)->phase;
    check(walked > 0.0f, "a walking unit's legs swing");

    // Now stop it by putting an enemy in reach, and let time keep passing.
    const Entity blocker = lanebattle::spawnUnit(game.world, false, kSoldier);
    const float walkerX = game.world.getComponent<Transform>(walker)->x;
    game.world.getComponent<Transform>(blocker)->x =
        walkerX + stats(kSoldier).range - 4.0f;
    game.driver.step(2);

    const float stopped = game.world.getComponent<Unit>(walker)->phase;
    game.driver.step(30);
    check(std::fabs(game.world.getComponent<Unit>(walker)->phase - stopped) < 0.01f,
          "a unit standing still does not walk on the spot");

    // A runner covers more ground per second, so its legs must cycle faster.
    Game other;
    other.startPlaying();
    other.suppressEnemySpawns();
    const Entity runner = lanebattle::spawnUnit(other.world, true, kRunner);
    other.driver.step(30);
    check(other.world.getComponent<Unit>(runner)->phase > walked,
          "and a faster unit's legs cycle faster");
}

void testLandingABlowSwingsTheArm() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity mine = lanebattle::spawnUnit(game.world, true, kSoldier);
    const Entity theirs = lanebattle::spawnUnit(game.world, false, kSoldier);
    game.world.getComponent<Transform>(mine)->x = 400.0f;
    game.world.getComponent<Transform>(theirs)->x =
        400.0f + stats(kSoldier).range - 4.0f;

    check(game.world.getComponent<Unit>(mine)->swing == 0.0f,
          "a unit that has not swung is at rest");

    game.driver.step(2);
    check(game.world.getComponent<Unit>(mine)->swing > 0.0f,
          "landing a blow starts the arm through its arc");

    // And the arc plays out rather than sticking.
    game.driver.step(30);
    check(game.world.getComponent<Unit>(mine)->swing >= 0.0f,
          "the swing never goes negative");
}

// The one thing about this slice that could rot silently: units are now two
// entities, and if the second one is not cleaned up, a long battle leaks one
// figure per death until the frame rate notices.
void testFiguresDoNotOutliveTheirUnits() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Entity doomed = lanebattle::spawnUnit(game.world, true, kSoldier);
    game.driver.step();
    check(lanebattle::countFigures(game.world) == 1, "it has a figure");

    game.world.getComponent<Unit>(doomed)->health = 0.0f;
    game.driver.step(3);

    check(lanebattle::countUnits(game.world, true) == 0, "the unit died");
    check(lanebattle::countFigures(game.world) == 0,
          "and took its figure with it");
}

// The same guard, but over a whole battle with dozens of deaths — the shape
// the leak would actually take.
void testNoFiguresLeakAcrossABattle() {
    Game game;
    game.startPlaying();

    for (int frame = 0; frame < 60 * 60; ++frame) {
        if (game.session().spawnCooldown <= 0.0f) {
            game.driver.tap(SDL_SCANCODE_2);
        }
        game.driver.step();
        if (game.session().gameOver) break;
    }

    const int units = lanebattle::countUnits(game.world, true) +
                      lanebattle::countUnits(game.world, false);
    check(lanebattle::countFigures(game.world) == units,
          "after a minute of fighting there is still exactly one figure per unit");
}

void testRestartingLeavesNoFiguresBehind() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    lanebattle::spawnUnit(game.world, true, kSoldier);
    lanebattle::spawnUnit(game.world, true, kRunner);
    game.driver.step();
    check(lanebattle::countFigures(game.world) == 2, "two figures on the field");

    // Lose, then restart.
    const Entity myCastle = lanebattle::findCastle(game.world, true);
    game.world.getComponent<Castle>(myCastle)->health = 0.0f;
    game.session().gameOver = true;
    game.driver.step(3);
    game.driver.tap(SDL_SCANCODE_R);
    game.driver.step(4);

    const int units = lanebattle::countUnits(game.world, true) +
                      lanebattle::countUnits(game.world, false);
    check(lanebattle::countFigures(game.world) == units,
          "a restart clears the old figures along with the old army");
}

// --- Scenery (slice 6) -----------------------------------------------------
//
// What the hills look like cannot be tested. What can: that they are at a
// range of depths, that they are wide enough not to run out, that nothing can
// mistake them for a unit, and that they are cleaned up on a restart.

void testSceneryIsAtSeveralDepths() {
    Game game;
    game.startPlaying();

    bool sawFar = false, sawMid = false, sawNear = false, sawForeground = false;
    for (Entity entity : game.world.entities()) {
        const Polygon* shape = game.world.getComponent<Polygon>(entity);
        if (!shape) continue;
        if (shape->parallax < 0.3f) sawFar = true;
        if (shape->parallax > 0.3f && shape->parallax < 0.6f) sawMid = true;
        if (shape->parallax > 0.6f && shape->parallax < 1.0f) sawNear = true;
        if (shape->parallax > 1.0f) sawForeground = true;
    }

    check(sawFar && sawMid && sawNear, "there are three bands behind the fight");
    check(sawForeground,
          "and one in front of it, moving faster than the ground");
}

// A band at parallax p slides kCameraMaxX*p pixels over a full sweep of the
// camera. Any narrower than the window plus that, and scrolling to the far end
// reveals the void behind it.
void testSceneryIsWideEnoughToCoverTheSweep() {
    Game game;
    game.startPlaying();

    float widest[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float bands[4] = {lanebattle::kFarParallax, lanebattle::kMidParallax,
                            lanebattle::kNearParallax, lanebattle::kForeParallax};

    for (Entity entity : game.world.entities()) {
        const Polygon* shape = game.world.getComponent<Polygon>(entity);
        const Transform* at = game.world.getComponent<Transform>(entity);
        if (!shape || !at) continue;
        for (int band = 0; band < 4; ++band) {
            if (std::fabs(shape->parallax - bands[band]) > 0.001f) continue;

            // The right edge of the shape, not of its Transform. Points are in
            // local space and a hill is drawn either side of its origin, so
            // measuring the origin alone understates the coverage by a
            // half-width — which is exactly the mistake this test made first
            // time and had to be corrected for.
            for (const Vec2& point : shape->points) {
                widest[band] = std::max(widest[band], at->x + point.x);
            }
        }
    }

    for (int band = 0; band < 4; ++band) {
        const float needed = static_cast<float>(lanebattle::kWindowWidth) +
                             lanebattle::kCameraMaxX * bands[band];
        check(widest[band] >= needed,
              "each band reaches far enough that scrolling never runs off it");
    }
}

// Scenery has no Team and no Unit, so nothing can walk up to a hill and
// attack it. Worth pinning: the targeting scan walks every entity in the
// world, and it is only the absence of those two components that saves it.
void testSceneryIsNotAValidTarget() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const int before = lanebattle::countUnits(game.world, true);
    const Entity unit = lanebattle::spawnUnit(game.world, true, kSoldier);
    game.driver.step(60);

    check(lanebattle::countUnits(game.world, true) == before + 1,
          "the unit is still alive");
    check(game.world.getComponent<Velocity>(unit)->dx > 0.0f,
          "and walks straight through the scenery rather than stopping to fight it");
}

// The HUD must stay locked to the screen now that there are four other
// scroll rates in play — this is the case parallax could quietly break.
void testTheHudStillIgnoresTheCameraEntirely() {
    Game game;
    game.startPlaying();

    for (Entity entity : game.world.entities()) {
        const Sprite* sprite = game.world.getComponent<Sprite>(entity);
        if (sprite && sprite->screenSpace) {
            check(engine::scrollFactor(sprite->screenSpace, sprite->parallax) == 0.0f,
                  "a screen-space sprite gets none of the camera, whatever its parallax");
        }
    }
    check(true, "checked every screen-space sprite");
}

void testRestartingClearsTheScenery() {
    Game game;
    game.startPlaying();

    int sceneryBefore = 0;
    for (Entity entity : game.world.entities()) {
        const Polygon* shape = game.world.getComponent<Polygon>(entity);
        if (shape && shape->parallax != 1.0f) ++sceneryBefore;
    }
    check(sceneryBefore > 0, "there is scenery to begin with");

    const Entity myCastle = lanebattle::findCastle(game.world, true);
    game.world.getComponent<Castle>(myCastle)->health = 0.0f;
    game.session().gameOver = true;
    game.driver.step(3);
    game.driver.tap(SDL_SCANCODE_R);
    game.driver.step(4);

    int sceneryAfter = 0;
    for (Entity entity : game.world.entities()) {
        const Polygon* shape = game.world.getComponent<Polygon>(entity);
        if (shape && shape->parallax != 1.0f) ++sceneryAfter;
    }
    check(sceneryAfter == sceneryBefore,
          "and exactly as much after a restart — no more, no fewer");
}

// --- Data-driven balance (slice 7) -----------------------------------------

std::string writeRoster(const char* name, const char* contents) {
    // Written next to the test binary rather than into a temp directory: it
    // needs no environment variable, it is the same place DataFile resolves
    // relative paths against, and it is cleaned up with the build.
    std::string path = name;
    if (char* base = SDL_GetBasePath()) {
        path = std::string(base) + name;
        SDL_free(base);
    }
    std::ofstream out(path);
    out << contents;
    out.close();
    return path;
}

// The defaults are what the game plays with when no file is found, which is
// the normal case for a bare build directory. Every other test in this file
// depends on them, so they are worth stating once.
void testTheDefaultsStandWithoutAFile() {
    lanebattle::resetBalance();
    check(lanebattle::unitKindCount() == lanebattle::kDefaultUnitKindCount,
          "the roster starts as the compiled-in defaults");

    check(!lanebattle::loadBalance("no/such/file/units.txt"),
          "a missing file reports failure");
    check(lanebattle::unitKindCount() == lanebattle::kDefaultUnitKindCount,
          "and changes nothing — a missing file is not an empty roster");
    check(stats(kSoldier).cost == lanebattle::kDefaultUnitKinds[1].cost,
          "the soldier still costs what the header says");
}

void testAFileOverridesTheDefaults() {
    lanebattle::resetBalance();
    const std::string path = writeRoster("lb_override.txt", R"(
[unit]
name = ARCHER
cost = 40
damage = 99
)");

    check(lanebattle::loadBalance(path), "the file loaded");
    check(lanebattle::unitKindCount() == lanebattle::kDefaultUnitKindCount,
          "overriding an existing name does not add a row");
    check(stats(kArcher).cost == 40.0f, "the value in the file wins");
    check(stats(kArcher).damage == 99.0f, "for every field it names");

    // The point of per-field fallbacks: a file that changes one number leaves
    // the rest of the row alone rather than zeroing it.
    check(stats(kArcher).range == lanebattle::kDefaultUnitKinds[2].range,
          "and a field the file does not mention keeps its old value");
    check(stats(kSoldier).cost == lanebattle::kDefaultUnitKinds[1].cost,
          "rows the file does not mention are untouched entirely");

    // Every field has its own fallback, so every field needs its own case.
    // Checking one of them proved nothing about the others: breaking the
    // fallback on `cost` alone left this whole file passing, because no test
    // had a row that named a unit without also naming its price.
    lanebattle::resetBalance();
    const std::string partial = writeRoster("lb_partial.txt", R"(
[unit]
name = SOLDIER
speed = 111
)");
    check(lanebattle::loadBalance(partial), "a one-field file loads");
    check(stats(kSoldier).speed == 111.0f, "the field it names changes");
    check(stats(kSoldier).cost == lanebattle::kDefaultUnitKinds[1].cost,
          "and the cost it does not name survives");
    check(stats(kSoldier).health == lanebattle::kDefaultUnitKinds[1].health,
          "as does the health");
    check(stats(kSoldier).damage == lanebattle::kDefaultUnitKinds[1].damage,
          "and the damage");
    check(std::string(stats(kSoldier).name) == "SOLDIER",
          "and it is still the same unit");

    lanebattle::resetBalance();
    check(stats(kSoldier).speed == lanebattle::kDefaultUnitKinds[1].speed,
          "and resetting puts the defaults back");
}

// A data file that can only edit rows is a config file. Being able to add a
// unit type without touching C++ is the actual point of the slice.
void testAFileCanAddAUnitType() {
    lanebattle::resetBalance();
    const std::string path = writeRoster("lb_added.txt", R"(
[unit]
name = KNIGHT
cost = 140
health = 260
damage = 22
speed = 60
)");

    check(lanebattle::loadBalance(path), "the file loaded");
    check(lanebattle::unitKindCount() == lanebattle::kDefaultUnitKindCount + 1,
          "an unrecognised name adds a row rather than being ignored");

    const int knight = lanebattle::kDefaultUnitKindCount;
    check(std::string(stats(knight).name) == "KNIGHT", "under the name it gave");
    check(stats(knight).health == 260.0f, "with the values it gave");

    // Unspecified fields come from the soldier, so a half-written row still
    // produces something that can walk and fight rather than a unit with no
    // reach that stands still and dies.
    check(stats(knight).range == lanebattle::kDefaultUnitKinds[1].range,
          "and sensible defaults for everything it left out");
    check(stats(knight).attackDelay > 0.0f, "including a usable attack delay");

    lanebattle::resetBalance();
}

void testAddedUnitsAreReachableInGame() {
    lanebattle::resetBalance();
    const std::string path = writeRoster("lb_playable.txt", R"(
[unit]
name = KNIGHT
cost = 20
health = 200
)");
    check(lanebattle::loadBalance(path), "the file loaded");

    // Deliberately NOT using Game, whose constructor resets the roster.
    World world;
    SceneStack scenes;
    harness::Harness driver(world, scenes);
    scenes.push(lanebattle::makePlayScene());
    driver.step(2);

    const int knight = lanebattle::kDefaultUnitKindCount;
    const Entity spawned = lanebattle::spawnUnit(world, true, knight);
    driver.step();

    check(lanebattle::countUnitsOfKind(world, true, knight) == 1,
          "a unit type that came from a file can be sent");
    check(world.getComponent<Unit>(spawned)->health == 200.0f,
          "and fights with the health the file gave it");
    check(lanebattle::buttonAt(
              lanebattle::buttonLeft(knight) + lanebattle::kButtonWidth / 2.0f,
              lanebattle::kButtonY + 4.0f) == knight,
          "and has a button on the spawn bar");

    lanebattle::resetBalance();
}

// A roster longer than the bar is wide would otherwise draw buttons off the
// side of the window, where they cannot be clicked and are not visible.
void testTheSpawnBarStopsAtTheEdgeOfTheWindow() {
    lanebattle::resetBalance();
    std::string contents;
    for (int extra = 0; extra < 12; ++extra) {
        contents += "[unit]\nname = EXTRA" + std::to_string(extra) + "\n\n";
    }
    const std::string path = writeRoster("lb_many.txt", contents.c_str());
    check(lanebattle::loadBalance(path), "a long roster loads");

    check(lanebattle::unitKindCount() > lanebattle::kMaxVisibleButtons,
          "there are more unit types than buttons that fit");
    check(lanebattle::visibleButtonCount() == lanebattle::kMaxVisibleButtons,
          "the bar shows as many as fit and no more");

    const float pastTheEnd =
        lanebattle::buttonLeft(lanebattle::kMaxVisibleButtons) + 4.0f;
    check(lanebattle::buttonAt(pastTheEnd, lanebattle::kButtonY + 4.0f) == -1,
          "and nothing is clickable past the last visible one");

    lanebattle::resetBalance();
}

// The file the player actually receives, not just the parser that reads it.
// Copied next to the test binary by CMake for exactly this.
void testTheShippedRosterIsSane() {
    lanebattle::resetBalance();
    check(lanebattle::loadBalance(lanebattle::kBalancePath),
          "the shipped roster file is where the game expects it");
    check(lanebattle::unitKindCount() >= 3, "and holds at least the three kinds");

    for (int kind = 0; kind < lanebattle::unitKindCount(); ++kind) {
        const lanebattle::UnitKind& row = stats(kind);
        check(row.cost > 0.0f && row.health > 0.0f && row.damage > 0.0f,
              "every shipped unit costs something and can fight");
        check(row.speed > 0.0f && row.range > 0.0f && row.attackDelay > 0.0f,
              "and can move and reach and swing");
        check(row.width > 0.0f && row.height > 0.0f, "and has a size");
    }

    lanebattle::resetBalance();
}

// --- The castle cannon (slice 8) -------------------------------------------

// Turns a world position into the screen position that would be clicked to
// aim at it. The camera is the only thing between them, which is exactly what
// screenToWorld undoes inside the game.
int screenXFor(Game& game, float worldX) {
    return static_cast<int>(worldX - game.cameraX());
}
constexpr int kFieldClickY = 410;  // on the ground, clear of every UI element

// A click is press-then-release, and the shot goes off on the release — the
// same edge that stops a held mouse button emptying the purse. So firing takes
// two frames, not one, and a test that steps once sees nothing.
constexpr int kFireFrames = 2;

// How many frames a shell is in the air, plus a little slack for it to land.
const int kFlightFrames =
    static_cast<int>(lanebattle::kCannonFlightTime * 60.0f) + 4;

// Where a unit will be by the time a shell arrives.
//
// Aiming at where a unit *is* misses it: the flight takes most of a second and
// a soldier covers eighty pixels in that time, which is well outside the blast.
// That is realistic, and it is also the mistake these tests made first time —
// they aimed at the start position and concluded the cannon did no damage.
float leadTarget(float startX, int kind, bool leftSide) {
    const float direction = leftSide ? 1.0f : -1.0f;
    return startX + direction * stats(kind).speed * lanebattle::kCannonFlightTime;
}

void testClickingTheFieldFiresTheCannon() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 500.0f;

    const float before = game.session().gold;
    game.driver.clickAt(screenXFor(game, 300.0f), kFieldClickY);
    game.driver.step(2);

    check(lanebattle::countCannonballs(game.world) == 1,
          "a click on the field puts a shot in the air");
    check(std::fabs((before - game.session().gold) - lanebattle::kCannonCost) < 2.0f,
          "and it costs what a shot costs");

    // The cooldown is what stops the cannon being the whole game.
    game.driver.clickAt(screenXFor(game, 320.0f), kFieldClickY);
    game.driver.step(2);
    check(lanebattle::countCannonballs(game.world) <= 1,
          "a second click during the cooldown fires nothing");
}

void testAnEmptyPurseFiresNothing() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = lanebattle::kCannonCost - 5.0f;

    game.driver.clickAt(screenXFor(game, 300.0f), kFieldClickY);
    game.driver.step(2);
    check(lanebattle::countCannonballs(game.world) == 0,
          "a cannon you cannot afford does not fire");
}

// A shot lands exactly where it was aimed, because the launch velocity is
// solved for the flight time rather than guessed. If that arithmetic is wrong,
// aiming becomes a feel to learn instead of a decision to make.
void testAShotLandsWhereItWasAimed() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 500.0f;

    const float aimAt = 380.0f;
    game.driver.clickAt(screenXFor(game, aimAt), kFieldClickY);
    game.driver.step(kFireFrames);

    Entity shot = kInvalidEntity;
    for (Entity entity : game.world.entities()) {
        if (game.world.hasComponent<lanebattle::Cannonball>(entity)) shot = entity;
    }
    check(shot != kInvalidEntity, "the shot exists");

    // Stopped a couple of frames short of landing, so it is still in the air
    // and can be measured before it destroys itself.
    game.driver.step(static_cast<int>(lanebattle::kCannonFlightTime * 60.0f) - 1);

    const Transform* at = game.world.getComponent<Transform>(shot);
    check(at != nullptr, "and is still airborne just before it lands");
    if (at) {
        check(std::fabs(at->x - aimAt) < 12.0f,
              "arriving within a few pixels of where it was aimed");
    }
}

// Each scenario gets its own battle with exactly one unit in it. Putting an
// enemy and a friendly side by side would have them fighting each other, and
// then "did the cannon hurt it" cannot be told apart from "did the soldier".
void testAShotDamagesEnemiesAndSparesFriends() {
    {
        Game game;
        game.startPlaying();
        game.suppressEnemySpawns();
        game.session().gold = 500.0f;

        // Inside the cannon.s reach: it fires from x=65 and reaches 420, so a
        // target past 485 is clamped short and the shell lands nowhere near it.
        // That is correct behaviour and it is what this test got wrong first
        // time, by standing the victim at 800.
        const Entity victim = lanebattle::spawnUnit(game.world, false, kSoldier);
        game.world.getComponent<Transform>(victim)->x = 400.0f;

        game.driver.clickAt(
            screenXFor(game, leadTarget(400.0f, kSoldier, false) + 12.0f),
            kFieldClickY);
        game.driver.step(kFireFrames + kFlightFrames);

        check(game.world.getComponent<Unit>(victim) != nullptr &&
                  game.world.getComponent<Unit>(victim)->health <
                      stats(kSoldier).health,
              "a shot damages an enemy inside the blast");
        check(lanebattle::countCannonballs(game.world) == 0,
              "and the shot is gone once it has landed");
    }
    {
        Game game;
        game.startPlaying();
        game.suppressEnemySpawns();
        game.session().gold = 500.0f;

        const Entity friendly = lanebattle::spawnUnit(game.world, true, kSoldier);
        game.world.getComponent<Transform>(friendly)->x = 300.0f;

        game.driver.clickAt(
            screenXFor(game, leadTarget(300.0f, kSoldier, true) + 12.0f),
            kFieldClickY);
        game.driver.step(kFireFrames + kFlightFrames);

        check(std::fabs(game.world.getComponent<Unit>(friendly)->health -
                        stats(kSoldier).health) < 0.01f,
              "and never damages your own army, however precisely it is aimed");
    }
    {
        Game game;
        game.startPlaying();
        game.suppressEnemySpawns();
        game.session().gold = 500.0f;

        const Entity distant = lanebattle::spawnUnit(game.world, false, kSoldier);
        game.world.getComponent<Transform>(distant)->x = 400.0f;

        // Aimed 200 pixels short of it: well outside a 46-pixel blast.
        game.driver.clickAt(screenXFor(game, 150.0f), kFieldClickY);
        game.driver.step(kFireFrames + kFlightFrames);

        check(std::fabs(game.world.getComponent<Unit>(distant)->health -
                        stats(kSoldier).health) < 0.01f,
              "and does not reach an enemy outside the blast");
    }
}

// Range is what keeps the cannon a defence rather than a way to contest the
// whole field. The first version reached 780 pixels, which covered a third of
// the world from each end, and measuring it showed the result: a mixed army
// that won in 195 seconds lost in 247, and every good strategy drew 800-800.
void testTheCannonCannotReachAcrossTheField() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 500.0f;

    const Entity faraway = lanebattle::spawnUnit(game.world, false, kSoldier);
    game.world.getComponent<Transform>(faraway)->x = 1600.0f;

    // Aimed WITH lead, so this is a shot that would land squarely on it if the
    // cannon could reach. Aiming at where it currently stands misses by the
    // width of its walk regardless of range, which made an earlier version of
    // this test pass even with the reach set ten times too far — it was
    // measuring the lead error, not the clamp.
    game.driver.clickAt(
        screenXFor(game, leadTarget(1600.0f, kSoldier, false) + 12.0f),
        kFieldClickY);
    game.driver.step(kFireFrames + kFlightFrames);

    check(std::fabs(game.world.getComponent<Unit>(faraway)->health -
                    stats(kSoldier).health) < 0.01f,
          "a click beyond the cannon's reach cannot hit what it pointed at");
}

// One button, two verbs. A press that moves is a camera drag and must not also
// fire, or scrolling the field would empty the purse.
void testDraggingDoesNotFire() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 500.0f;

    game.driver.moveMouse(600, kFieldClickY);
    game.driver.pressMouse();
    game.driver.step();
    game.driver.moveMouse(300, kFieldClickY);
    game.driver.step();
    game.driver.releaseMouse();
    game.driver.step(2);

    check(lanebattle::countCannonballs(game.world) == 0,
          "a drag scrolls the view and fires nothing");
    check(game.session().gold >= 490.0f, "and costs nothing");
}

void testClickingTheUiDoesNotFire() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 500.0f;

    game.driver.clickAt(static_cast<int>(buttonCenterX(kSoldier)),
                        static_cast<int>(buttonCenterY()));
    game.driver.step(2);
    check(lanebattle::countCannonballs(game.world) == 0,
          "clicking the spawn bar sends a unit, not a shell");

    game.driver.step(30);
    game.driver.clickAt(static_cast<int>(lanebattle::kUpgradeX + 10),
                        static_cast<int>(lanebattle::upgradeTop(0) + 8));
    game.driver.step(2);
    check(lanebattle::countCannonballs(game.world) == 0,
          "and clicking an upgrade buys it, not a shell");
}

void testTheEnemyCastleShootsBack() {
    Game game;
    game.startPlaying();
    game.session().enemySpawnTimer = 1.0e9f;   // no units, but leave its gun on
    game.session().enemyGold = 100000.0f;
    game.session().enemyCannonCooldown = 0.0f;

    // A unit right up against the enemy castle is what its cannon is for.
    const Entity attacker = lanebattle::spawnUnit(game.world, true, kSoldier);
    const Entity enemyCastle = lanebattle::findCastle(game.world, false);
    // Close enough that it stops to hit the castle. A unit still walking
    // would be somewhere else by the time the shell arrived, and this is
    // about whether the gun fires at all, not about leading a target.
    game.world.getComponent<Transform>(attacker)->x =
        game.world.getComponent<Transform>(enemyCastle)->x - 50.0f;

    game.driver.step(kFlightFrames + 6);

    check(game.world.getComponent<Unit>(attacker) == nullptr ||
              game.world.getComponent<Unit>(attacker)->health <
                  stats(kSoldier).health,
          "the enemy castle shells whatever walks up to it");
}

void testCannonballsDoNotSurviveARestart() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 500.0f;

    game.driver.clickAt(screenXFor(game, 300.0f), kFieldClickY);
    game.driver.step(kFireFrames);
    check(lanebattle::countCannonballs(game.world) == 1, "a shot is in the air");

    const Entity myCastle = lanebattle::findCastle(game.world, true);
    game.world.getComponent<Castle>(myCastle)->health = 0.0f;
    game.session().gameOver = true;
    game.driver.step(3);
    game.driver.tap(SDL_SCANCODE_R);
    game.driver.step(4);

    check(lanebattle::countCannonballs(game.world) == 0,
          "and does not survive into the next battle");
}

// --- In-battle upgrades (slice 8) ------------------------------------------

void testUpgradeCostsRise() {
    for (int index = 0; index < lanebattle::kUpgradeCount; ++index) {
        const float first = lanebattle::upgradeCost(index, 0);
        const float second = lanebattle::upgradeCost(index, 1);
        const float fifth = lanebattle::upgradeCost(index, 4);
        check(first > 0.0f, "the first level costs something");
        check(second > first, "the second costs more than the first");
        check(fifth > second * 1.5f,
              "and the fifth costs enough that buying everything is not free");
    }
}

void testClickingAnUpgradeBuysIt() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 5000.0f;

    const int income = static_cast<int>(lanebattle::Upgrade::Income);
    const float cost = lanebattle::upgradeCost(income, 0);
    const float before = game.session().gold;

    game.driver.clickAt(static_cast<int>(lanebattle::kUpgradeX + 10),
                        static_cast<int>(lanebattle::upgradeTop(income) + 8));
    game.driver.step(2);

    check(game.session().upgrades[income] == 1, "the upgrade was bought");
    check(std::fabs((before - game.session().gold) - cost) < 3.0f,
          "and charged at its listed price");
}

void testAnUnaffordableUpgradeDoesNothing() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 10.0f;

    game.driver.clickAt(static_cast<int>(lanebattle::kUpgradeX + 10),
                        static_cast<int>(lanebattle::upgradeTop(0) + 8));
    game.driver.step(2);

    check(game.session().upgrades[0] == 0, "an upgrade you cannot afford is not bought");
    check(game.session().gold > 0.0f, "and takes no money");
}

void testUpgradeHitTesting() {
    for (int index = 0; index < lanebattle::kUpgradeCount; ++index) {
        check(lanebattle::upgradeAt(lanebattle::kUpgradeX + 10.0f,
                                    lanebattle::upgradeTop(index) + 8.0f) == index,
              "each upgrade row is its own button");
    }
    check(lanebattle::upgradeAt(400.0f, lanebattle::upgradeTop(0) + 8.0f) == -1,
          "the field to the left of the panel is not a button");
    check(lanebattle::upgradeAt(lanebattle::kUpgradeX + 10.0f, 400.0f) == -1,
          "and neither is the field below it");
}

void testIncomeSupplyAndWallsAllDoSomething() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();

    const Session& session = game.session();
    const float baseIncome = lanebattle::goldPerSecondFor(session, true);
    const int baseCap = lanebattle::populationCapFor(session, true);
    const float baseWalls = lanebattle::castleMaxHealthFor(session, true);

    game.session().upgrades[static_cast<int>(lanebattle::Upgrade::Income)] = 1;
    check(lanebattle::goldPerSecondFor(session, true) > baseIncome,
          "INCOME raises the rate gold arrives at");

    game.session().upgrades[static_cast<int>(lanebattle::Upgrade::Supply)] = 1;
    check(lanebattle::populationCapFor(session, true) > baseCap,
          "SUPPLY raises how many units you may field");

    game.session().upgrades[static_cast<int>(lanebattle::Upgrade::Walls)] = 1;
    check(lanebattle::castleMaxHealthFor(session, true) > baseWalls,
          "WALLS raises how much punishment your castle takes");

    // And each side reads its own levels, which is the kind of thing that goes
    // wrong silently and made slice 1 unwinnable.
    check(lanebattle::goldPerSecondFor(session, false) == baseIncome,
          "your upgrades do not enrich the opponent");
    check(lanebattle::populationCapFor(session, false) == baseCap,
          "nor raise its cap");
}

// WALLS is the one upgrade whose effect is a level rather than a rate. Raising
// the maximum is worth nothing to a castle that is already damaged, so the
// stonework has to be healed on at the moment it is bought.
void testBuyingWallsHealsTheCastle() {
    Game game;
    game.startPlaying();
    game.suppressEnemySpawns();
    game.session().gold = 5000.0f;

    const Entity castle = lanebattle::findCastle(game.world, true);
    game.world.getComponent<Castle>(castle)->health = 200.0f;

    const int walls = static_cast<int>(lanebattle::Upgrade::Walls);
    game.driver.clickAt(static_cast<int>(lanebattle::kUpgradeX + 10),
                        static_cast<int>(lanebattle::upgradeTop(walls) + 8));
    game.driver.step(2);

    check(game.session().upgrades[walls] == 1, "WALLS was bought");
    check(game.castleHealth(true) > 400.0f,
          "and the new stonework is standing rather than merely permitted");
}

// An opponent that cannot upgrade loses every long game by construction — the
// same shape of asymmetry that made slice 1 unwinnable.
// Upgrade numbers are data, exactly as unit stats are. What a file may NOT do
// is add an upgrade: each one has its own rule in code, so a name the game
// does not know is a typo rather than a fourth feature.
void testUpgradesAreDataDriven() {
    lanebattle::resetBalance();
    const float defaultCost = lanebattle::upgradeCost(0, 0);

    const std::string path = writeRoster("lb_upgrades.txt", R"(
[upgrade]
name   = INCOME
cost   = 40
effect = 11

[upgrade]
name   = NOT_A_REAL_UPGRADE
cost   = 1
)");
    check(lanebattle::loadBalance(path), "the file loaded");

    const int income = static_cast<int>(lanebattle::Upgrade::Income);
    check(lanebattle::upgradeCost(income, 0) == 40.0f,
          "a file can retune what an upgrade costs");
    check(lanebattle::upgradeKind(income).effect == 11.0f,
          "and what one level of it is worth");
    check(lanebattle::upgradeKind(income).costGrowth ==
              lanebattle::kDefaultUpgrades[income].costGrowth,
          "while a field it does not mention keeps its value");
    check(lanebattle::kUpgradeCount == 3,
          "and an unrecognised upgrade name adds nothing");

    lanebattle::resetBalance();
    check(lanebattle::upgradeCost(0, 0) == defaultCost,
          "resetting puts the upgrade table back too");
}

// The effect a file sets is the effect the game plays with, not just the one
// the panel prints. Worth its own case: reading the table in the UI and
// hard-coding it in the rules would look right and play wrong.
void testARetunedUpgradeActuallyChangesTheGame() {
    lanebattle::resetBalance();
    const std::string path = writeRoster("lb_bigsupply.txt", R"(
[upgrade]
name   = SUPPLY
effect = 25
)");
    check(lanebattle::loadBalance(path), "the file loaded");

    World world;
    SceneStack scenes;
    harness::Harness driver(world, scenes);
    scenes.push(lanebattle::makePlayScene());
    driver.step(2);

    Session* session = lanebattle::findSession(world);
    const int base = lanebattle::populationCapFor(*session, true);
    session->upgrades[static_cast<int>(lanebattle::Upgrade::Supply)] = 1;
    check(lanebattle::populationCapFor(*session, true) == base + 25,
          "one level of a retuned SUPPLY is worth what the file says");

    lanebattle::resetBalance();
}

void testTheEnemyUpgradesToo() {
    Game game;
    game.startPlaying();
    game.session().enemyGold = 100000.0f;

    game.driver.step(60 * 5);

    int total = 0;
    for (int index = 0; index < lanebattle::kUpgradeCount; ++index) {
        total += game.session().enemyUpgrades[index];
    }
    check(total > 0, "the opponent buys upgrades of its own");
}

// --- Can the game actually be played? --------------------------------------

// Plays a whole battle on a fixed composition, sending each unit as soon as it
// is affordable. Returns +1 for a win, -1 for a loss, 0 for neither inside the
// time limit.
int playBattle(Game& game, const int* cycle, int cycleLength) {
    const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3};
    int index = 0;
    SDL_Scancode holding = SDL_SCANCODE_UNKNOWN;

    for (int frame = 0; frame < 60 * 400; ++frame) {
        if (holding != SDL_SCANCODE_UNKNOWN) {
            game.driver.release(holding);
            holding = SDL_SCANCODE_UNKNOWN;
        }
        const int kind = cycle[index % cycleLength];
        if (game.session().spawnCooldown <= 0.0f &&
            game.session().gold >= stats(kind).cost) {
            holding = keys[kind];
            game.driver.hold(holding);
            index = (index + 1) % cycleLength;
        }
        game.driver.step();
        if (game.session().gameOver) return game.session().playerWon ? 1 : -1;
    }
    return 0;
}
//
// Every test above checks a rule in isolation: this one checks that the rules
// add up to a game that ends. It exists because nothing did, and the gap hid
// something the individual tests could never have caught.
//
// Simulating a whole battle showed the two sides deadlocked at the exact same
// two pixel positions for four solid minutes, castles untouched, gold cycling
// on a perfect period. Spending the instant you can afford to is precisely
// what the opponent does, so playing that way mirrors it exactly and neither
// front line ever moves.
//
// Banking the gold and sending a wave breaks it immediately — the extra
// bodies win the trade at the front and the line rolls forward. That is a real
// strategy and it is the whole of the game's depth at the moment, which is a
// finding about the DESIGN rather than a bug. What is pinned here is only the
// part that must never stop being true: that the battle can be won at all.
void testABattleCanBeWon() {
    static const int mixed[] = {kSoldier, kSoldier, kArcher};
    Game game;
    game.startPlaying();

    const int outcome = playBattle(game, mixed, 3);
    check(outcome != 0, "a battle fought with a mixed force ends");
    check(outcome == 1, "and a front line with archers behind it wins");
}

// The design guard for the whole slice.
//
// Slices 1 and 2 had one unit type, and measuring showed the consequence: the
// only decision was when to spend, both sides traded evenly, and a player who
// mirrored the opponent deadlocked the field forever. The point of a roster is
// that no single row of it is a strategy.
//
// This test asserts a balance property rather than a rule, which makes it the
// most fragile thing in this file — and that is deliberate. If a retune ever
// makes one unit type sufficient on its own, the slice's reason for existing
// is gone and this should be what says so.
void testOneUnitTypeIsNotEnough() {
    static const int onlySoldiers[] = {kSoldier};
    static const int onlyRunners[] = {kRunner};
    static const int onlyArchers[] = {kArcher};

    Game soldiers;
    soldiers.startPlaying();
    check(playBattle(soldiers, onlySoldiers, 1) == -1,
          "an army of nothing but soldiers loses");

    Game runners;
    runners.startPlaying();
    check(playBattle(runners, onlyRunners, 1) == -1,
          "an army of nothing but runners loses");

    Game archers;
    archers.startPlaying();
    check(playBattle(archers, onlyArchers, 1) == -1,
          "and archers with nobody to hide behind lose fastest of all");
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
    testTheEnemyBanksBeforeSpending();
    testTheEnemyMixesItsWave();
    testTheEnemyPaysForItsUnits();
    testTheEconomiesAreSymmetric();
    testPauseFreezesTheBattle();
    testRestartingAfterDefeat();

    testTheFieldIsWiderThanTheWindow();
    testTheCameraStartsOnYourOwnCastle();
    testTheCameraFollowsYourFrontLine();
    testTheCameraStopsAtTheEdgesOfTheWorld();
    testArrowKeysTakeTheViewOffTheLeash();
    testFreeLookRespectsTheWorldEdges();
    testTheHudIgnoresTheCamera();
    testTheMinimapTracksTheFrontLines();

    testEachKeySendsItsOwnKind();
    testKindsCostWhatTheTableSays();
    testThePopulationCapHolds();
    testUnitsHoldRank();
    testMeleeWalksPastItsOwnArchers();
    testArchersOutrangeSoldiers();
    testTargetTiesGoToTheLowerEntityId();
    testKillsPayGold();

    testButtonHitTesting();
    testClickingAButtonSendsItsUnit();
    testHoldingTheMouseDoesNotEmptyThePurse();
    testClickingTheFieldBuysNothing();
    testAnUnaffordableButtonDoesNothing();
    testDraggingScrollsTheView();
    testDraggingFromTheBarDoesNotScroll();
    testTheKeysStillWork();

    testEveryUnitHasAFigure();
    testAFigureFollowsItsUnit();
    testLegsMoveWithTheUnitNotWithTheClock();
    testLandingABlowSwingsTheArm();
    testFiguresDoNotOutliveTheirUnits();
    testNoFiguresLeakAcrossABattle();
    testRestartingLeavesNoFiguresBehind();

    testSceneryIsAtSeveralDepths();
    testSceneryIsWideEnoughToCoverTheSweep();
    testSceneryIsNotAValidTarget();
    testTheHudStillIgnoresTheCameraEntirely();
    testRestartingClearsTheScenery();

    testTheDefaultsStandWithoutAFile();
    testAFileOverridesTheDefaults();
    testAFileCanAddAUnitType();
    testAddedUnitsAreReachableInGame();
    testTheSpawnBarStopsAtTheEdgeOfTheWindow();
    testTheShippedRosterIsSane();

    testClickingTheFieldFiresTheCannon();
    testAnEmptyPurseFiresNothing();
    testAShotLandsWhereItWasAimed();
    testAShotDamagesEnemiesAndSparesFriends();
    testTheCannonCannotReachAcrossTheField();
    testDraggingDoesNotFire();
    testClickingTheUiDoesNotFire();
    testTheEnemyCastleShootsBack();
    testCannonballsDoNotSurviveARestart();

    testUpgradeCostsRise();
    testClickingAnUpgradeBuysIt();
    testAnUnaffordableUpgradeDoesNothing();
    testUpgradeHitTesting();
    testIncomeSupplyAndWallsAllDoSomething();
    testBuyingWallsHealsTheCastle();
    testUpgradesAreDataDriven();
    testARetunedUpgradeActuallyChangesTheGame();
    testTheEnemyUpgradesToo();

    testABattleCanBeWon();
    testOneUnitTypeIsNotEnough();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
