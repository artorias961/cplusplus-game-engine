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

// Names for the rows of the roster, so the tests read as prose rather than as
// indices. `stats(kSoldier).range` says what it means; `kUnitKinds[1].range`
// does not.
constexpr int kRunner = 0;
constexpr int kSoldier = 1;
constexpr int kArcher = 2;

const lanebattle::UnitKind& stats(int kind) {
    return lanebattle::kUnitKinds[kind];
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

    Game() : scenes(), driver(world, scenes) {}

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
    for (int kind = 0; kind < lanebattle::kUnitKindCount; ++kind) {
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

    game.driver.hold(SDL_SCANCODE_1);
    game.driver.step(60 * 20);  // far more presses than the cap allows
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
    for (int kind = 0; kind < lanebattle::kUnitKindCount; ++kind) {
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

    for (int kind = 0; kind < lanebattle::kUnitKindCount; ++kind) {
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

    testABattleCanBeWon();
    testOneUnitTypeIsNotEnough();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
