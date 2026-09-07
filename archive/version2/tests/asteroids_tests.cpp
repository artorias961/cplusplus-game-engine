// Before ANY include: Asteroids.h reaches SDL.h through the engine headers,
// and SDL.h renames main() unless this is already defined.
#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// asteroids_tests.cpp — the game's rules, checked without opening a window.
//
// These are the paths that were untestable while Asteroids was a single
// main.cpp: clearing a wave, dying, respawning (including refusing to respawn
// into a rock), running out of lives, restarting, and pausing.
//
// No sound device or artwork is wired up here, which is itself part of the
// test: the game has to run without either.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdio>

#include "Asteroids.h"
#include "Harness.h"

using namespace engine;
using asteroids::Rock;
using asteroids::Session;

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

    Game() : scenes(), driver(world, scenes) {
        // Same waves every run. Without this the rocks are placed from a
        // system seed, and a test that survives one run can crash the next.
        asteroids::setRandomSeed(20260906u);
    }

    void startPlaying() {
        scenes.push(asteroids::makeTitleScene());
        driver.step();
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
    }

    // Ends spawn protection immediately.
    //
    // These tests used to run 180 frames to let it lapse on its own, which
    // meant three seconds of randomly placed rocks drifting around a ship that
    // became vulnerable partway through. About one run in twelve, one of them
    // reached the ship first and the test then dereferenced a ship that no
    // longer existed. Setting the field is instant and cannot be interfered
    // with.
    void endSpawnProtection() { session().spawnProtection = 0.0f; }

    Session& session() { return *asteroids::findSession(world); }
    Entity ship() { return asteroids::findShip(world); }

    // Clears the field so a test can set up the exact situation it wants.
    void removeAllRocks() {
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Rock>(entity)) world.destroyLater(entity);
        }
    }
};

void testStartingAWave() {
    Game game;
    game.startPlaying();

    check(asteroids::findSession(game.world) != nullptr,
          "starting play creates a session");
    check(game.session().wave == 1, "play begins on wave one");
    check(asteroids::countRocks(game.world) == asteroids::kStartRocks,
          "the first wave has the starting number of rocks");
    check(game.ship() != kInvalidEntity, "there is a ship");
    check(game.session().lives == asteroids::kStartLives, "lives start full");
}

void testFiringCreatesBulletsThatExpire() {
    Game game;
    game.startPlaying();

    check(asteroids::countBullets(game.world) == 0, "no bullets to begin with");

    game.driver.hold(SDL_SCANCODE_SPACE);
    game.driver.step(4);
    check(asteroids::countBullets(game.world) > 0, "holding fire makes bullets");
    game.driver.release(SDL_SCANCODE_SPACE);

    // Each bullet carries a Lifetime, so they clear themselves without the
    // game keeping any list of them.
    game.driver.step(120);  // two seconds, well past kBulletLife
    check(asteroids::countBullets(game.world) == 0,
          "bullets expire on their own");
}

void testThrustAccelerates() {
    Game game;
    game.startPlaying();

    Velocity& velocity = *game.world.getComponent<Velocity>(game.ship());
    check(std::hypot(velocity.dx, velocity.dy) < 0.01f, "the ship starts still");

    game.driver.hold(SDL_SCANCODE_UP);
    game.driver.step(20);
    const float movingSpeed = std::hypot(velocity.dx, velocity.dy);
    check(movingSpeed > 10.0f, "thrust builds speed");

    // Drag, not an instant stop: releasing thrust leaves it coasting.
    game.driver.release(SDL_SCANCODE_UP);
    game.driver.step(5);
    const float coasting = std::hypot(velocity.dx, velocity.dy);
    check(coasting > 0.0f, "the ship keeps drifting after thrust stops");
    check(coasting < movingSpeed, "drag slows it down");
}

void testClearingAWaveStartsTheNext() {
    Game game;
    game.startPlaying();

    game.removeAllRocks();
    game.driver.step(3);

    check(game.session().wave == 2, "an empty field advances the wave");
    check(asteroids::countRocks(game.world) == asteroids::kStartRocks + 1,
          "each wave arrives one rock larger");
}

void testCollidingWithARockCostsALife() {
    Game game;
    game.startPlaying();

    // Spawn protection has to run out before the ship is vulnerable.
    game.endSpawnProtection();
    check(game.session().spawnProtection <= 0.0f, "spawn protection can be ended");

    game.removeAllRocks();
    game.driver.step();

    // Drop a rock exactly on the ship. Guarded, so that if setup ever stops
    // leaving a ship alive this reports a failure instead of crashing.
    if (game.ship() == kInvalidEntity) {
        check(false, "the ship survived setup");
        return;
    }
    Transform shipAt = *game.world.getComponent<Transform>(game.ship());
    Entity rock = game.world.createEntity();
    game.world.addComponent(rock, Transform{shipAt.x, shipAt.y, 0.0f});
    game.world.addComponent(rock, Velocity{});
    game.world.addComponent(rock, CircleCollider{asteroids::kRockRadius[2]});
    game.world.addComponent(rock, Rock{asteroids::kLargeRock});

    game.driver.step(2);

    check(game.session().lives == asteroids::kStartLives - 1,
          "hitting a rock costs a life");
    check(game.ship() == kInvalidEntity, "the ship is destroyed");
}

// The respawn fix: a new ship waits for room rather than appearing inside a
// rock and dying again the moment protection wears off.
void testRespawnWaitsForAClearSpace() {
    Game game;
    game.startPlaying();
    game.endSpawnProtection();
    game.removeAllRocks();
    game.driver.step();

    // Park a motionless rock in the middle, where a ship respawns, and kill
    // the ship with it.
    const float centerX = static_cast<float>(asteroids::kWindowWidth) / 2.0f;
    const float centerY = static_cast<float>(asteroids::kWindowHeight) / 2.0f;
    Entity blocker = game.world.createEntity();
    game.world.addComponent(blocker, Transform{centerX, centerY, 0.0f});
    game.world.addComponent(blocker, Velocity{});
    game.world.addComponent(blocker, CircleCollider{asteroids::kRockRadius[2]});
    game.world.addComponent(blocker, Rock{asteroids::kLargeRock});

    game.driver.step(2);
    check(game.ship() == kInvalidEntity, "the parked rock destroys the ship");

    // kRespawnDelay is 1.4s — about 84 frames. Well past it, and still no
    // ship, because the spawn point is occupied.
    game.driver.step(150);
    check(game.ship() == kInvalidEntity,
          "no ship returns while a rock sits on the spawn point");
    check(game.session().lives > 0, "and no further lives are lost meanwhile");

    // Clear the blocker and it comes back promptly.
    game.world.destroyLater(blocker);
    game.driver.step(5);
    check(game.ship() != kInvalidEntity,
          "the ship returns once the space is clear");
}

void testRunningOutOfLivesAndRestarting() {
    Game game;
    game.startPlaying();
    game.endSpawnProtection();

    game.session().lives = 1;
    game.session().score = 500;
    game.removeAllRocks();
    game.driver.step();

    if (game.ship() == kInvalidEntity) {
        check(false, "the ship survived setup");
        return;
    }
    Transform shipAt = *game.world.getComponent<Transform>(game.ship());
    Entity rock = game.world.createEntity();
    game.world.addComponent(rock, Transform{shipAt.x, shipAt.y, 0.0f});
    game.world.addComponent(rock, Velocity{});
    game.world.addComponent(rock, CircleCollider{asteroids::kRockRadius[2]});
    game.world.addComponent(rock, Rock{asteroids::kLargeRock});

    game.driver.step(2);
    check(game.session().gameOver, "the last life ends the game");

    game.driver.tap(SDL_SCANCODE_R);
    game.driver.step(3);

    check(!game.session().gameOver, "R clears the game-over state");
    check(game.session().lives == asteroids::kStartLives, "lives are restored");
    check(game.session().score == 0, "the score resets");
    check(game.session().wave == 1, "the wave counter resets");
    check(game.ship() != kInvalidEntity, "a fresh ship is flying");
}

void testPauseFreezesPlay() {
    Game game;
    game.startPlaying();

    // Spawn protection is left running here on purpose: it keeps the ship
    // alive while the test thrusts around, so nothing below can hit a ship
    // that no longer exists.
    game.driver.hold(SDL_SCANCODE_UP);
    game.driver.step(10);
    game.driver.release(SDL_SCANCODE_UP);

    game.driver.tap(SDL_SCANCODE_P);
    game.driver.step(2);

    const Transform frozen = *game.world.getComponent<Transform>(game.ship());
    game.driver.step(30);
    const Transform after = *game.world.getComponent<Transform>(game.ship());
    check(std::fabs(frozen.x - after.x) < 0.01f &&
              std::fabs(frozen.y - after.y) < 0.01f,
          "nothing moves while paused");
    check(game.ship() != kInvalidEntity,
          "pausing keeps the field alive rather than unloading it");

    game.driver.tap(SDL_SCANCODE_P);
    game.driver.step(5);
    const Transform resumed = *game.world.getComponent<Transform>(game.ship());
    check(std::fabs(resumed.x - after.x) > 0.001f ||
              std::fabs(resumed.y - after.y) > 0.001f,
          "unpausing resumes drifting where it left off");
}

}  // namespace

int main() {
    std::printf("asteroids tests\n");

    testStartingAWave();
    testFiringCreatesBulletsThatExpire();
    testThrustAccelerates();
    testClearingAWaveStartsTheNext();
    testCollidingWithARockCostsALife();
    testRespawnWaitsForAClearSpace();
    testRunningOutOfLivesAndRestarting();
    testPauseFreezesPlay();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
