// ---------------------------------------------------------------------------
// breakout_tests.cpp — the game's rules, checked without opening a window.
//
// engine_tests.cpp covers the engine's arithmetic. This covers the parts that
// only exist when scenes, input and the world run together: starting a round,
// launching, breaking bricks, losing a life, running out of lives, restarting,
// pausing, and clearing a field. Those are exactly the paths that stayed
// untested in Snake and Asteroids, because their logic is locked inside a
// main.cpp behind a window.
//
// Every test here runs in microseconds and gives the same answer every time.
// ---------------------------------------------------------------------------

// Before ANY include: Breakout.h reaches SDL.h through the engine headers, and
// SDL.h renames main() unless this is already defined.
#define SDL_MAIN_HANDLED

#include <cmath>
#include <cstdio>

#include "Breakout.h"
#include "Harness.h"

using namespace engine;
using breakout::Ball;
using breakout::Brick;
using breakout::Session;

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

// Every test starts here: title screen, then Space to begin play.
struct Game {
    World world;
    SceneStack scenes;
    harness::Harness driver;

    Game() : scenes(), driver(world, scenes) {}

    void startPlaying() {
        scenes.push(breakout::makeTitleScene());
        driver.step();          // enters the title scene
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);         // title reads the key, then swaps in play
    }

    Session& session() { return *breakout::findSession(world); }
    Entity ball() { return breakout::findBall(world); }
    Transform& ballAt() { return *world.getComponent<Transform>(ball()); }
    Ball& ballData() { return *world.getComponent<Ball>(ball()); }
};

void testStartingARound() {
    Game game;
    game.startPlaying();

    check(breakout::findSession(game.world) != nullptr,
          "starting play creates a session");
    check(breakout::countBricks(game.world) ==
              breakout::kBrickColumns * breakout::kBrickRows,
          "the field starts full of bricks");
    check(game.ball() != kInvalidEntity, "there is a ball");
    check(breakout::findPaddle(game.world) != kInvalidEntity,
          "there is a paddle");
    check(game.session().lives == breakout::kStartLives, "lives start full");
    check(game.session().score == 0, "the score starts at zero");
}

void testPaddleMovesAndIsClamped() {
    Game game;
    game.startPlaying();

    Transform& paddle = *game.world.getComponent<Transform>(
        breakout::findPaddle(game.world));
    const float startX = paddle.x;

    game.driver.hold(SDL_SCANCODE_RIGHT);
    game.driver.step(10);
    check(paddle.x > startX, "holding right moves the paddle right");

    game.driver.release(SDL_SCANCODE_RIGHT);
    game.driver.hold(SDL_SCANCODE_LEFT);
    game.driver.step(240);  // four seconds: far enough to pin it to the wall
    check(paddle.x >= static_cast<float>(breakout::kWallThickness) - 0.01f,
          "the paddle cannot be driven through the left wall");
}

void testBallRidesPaddleUntilLaunched() {
    Game game;
    game.startPlaying();

    check(game.ballData().stuckToPaddle, "the ball starts stuck to the paddle");

    const float before = game.ballAt().y;
    game.driver.step(30);
    check(std::fabs(game.ballAt().y - before) < 0.01f,
          "an unlaunched ball does not fall or drift");

    game.driver.hold(SDL_SCANCODE_RIGHT);
    game.driver.step(10);
    game.driver.release(SDL_SCANCODE_RIGHT);
    const float carriedX = game.ballAt().x;
    Transform& paddle = *game.world.getComponent<Transform>(
        breakout::findPaddle(game.world));
    check(std::fabs(carriedX - (paddle.x + breakout::kPaddleWidth / 2.0f)) < 0.01f,
          "the ball is carried on the middle of the paddle");

    game.driver.tap(SDL_SCANCODE_SPACE);
    game.driver.step(2);
    check(!game.ballData().stuckToPaddle, "space launches the ball");
    check(game.ballData().velocity.y < 0.0f, "the ball launches upward");
}

void testBallBreaksBricksAndScores() {
    Game game;
    game.startPlaying();

    const int bricksBefore = breakout::countBricks(game.world);

    // Put the ball just below the bottom row and send it straight up, so the
    // hit is certain rather than a matter of luck.
    Ball& ball = game.ballData();
    Transform& at = game.ballAt();
    ball.stuckToPaddle = false;
    at.x = static_cast<float>(breakout::kWallThickness + breakout::kFieldMargin) +
           breakout::brickWidth() / 2.0f;
    at.y = static_cast<float>(breakout::kBrickTop +
                              breakout::kBrickRows *
                                  (breakout::kBrickHeight + breakout::kBrickGap)) +
           40.0f;
    ball.velocity = Vec2{0.0f, -300.0f};

    game.driver.step(30);

    check(breakout::countBricks(game.world) == bricksBefore - 1,
          "hitting a brick destroys exactly one");
    check(game.session().score == breakout::kBrickScore,
          "breaking a brick scores");
    check(game.ballData().velocity.y > 0.0f,
          "the ball bounces back downward off the underside of a brick");
}

// The reason moveBall() takes sub-frame steps at all.
//
// This needs an ISOLATED target. With the field full, the rows sit 30 pixels
// apart and the ball is 16 across, so a huge jump lands overlapping some row
// or other by luck and the test passes whether or not substepping works —
// which is exactly what the first version of this test did. One lone brick
// with open space behind it is the case that actually distinguishes them.
void testFastBallDoesNotTunnel() {
    Game game;
    game.startPlaying();

    // Clear the field down to a single brick, remembering where it is.
    Entity survivor = kInvalidEntity;
    for (Entity entity : game.world.entities()) {
        if (!game.world.hasComponent<Brick>(entity)) continue;
        if (survivor == kInvalidEntity) {
            survivor = entity;
            continue;
        }
        game.world.destroyLater(entity);
    }
    game.driver.step();
    check(breakout::countBricks(game.world) == 1, "exactly one brick remains");

    const Transform brickAt = *game.world.getComponent<Transform>(survivor);
    const int scoreBefore = game.session().score;

    Ball& ball = game.ballData();
    Transform& at = game.ballAt();
    ball.stuckToPaddle = false;
    at.x = brickAt.x + breakout::brickWidth() / 2.0f;
    at.y = brickAt.y + static_cast<float>(breakout::kBrickHeight) + 20.0f;

    // 3000 px/s across a 1/30s frame is 100 pixels of travel. The brick is 24
    // tall, so a single jump starts 20 below it and ends 56 above it, never
    // overlapping at either end: without substepping the ball passes clean
    // through and the brick survives.
    ball.velocity = Vec2{0.0f, -3000.0f};
    game.driver.step(1, 1.0f / 30.0f);

    // Checked by score rather than brick count, because destroying the last
    // brick immediately refills the field for the next level.
    check(game.session().score == scoreBefore + breakout::kBrickScore,
          "a ball moving far further than a brick is thick still hits it");
}

// Repeatedly clipping the same side of the paddle used to flatten the ball's
// path until it was crawling almost horizontally between the side walls. The
// paddle adds only sideways speed and the total is rescaled, so the vertical
// share shrinks a little with every off-centre hit and compounds.
void testBallNeverFlattensOut() {
    Game game;
    game.startPlaying();

    Ball& ball = game.ballData();
    ball.stuckToPaddle = false;

    Transform& paddle = *game.world.getComponent<Transform>(
        breakout::findPaddle(game.world));

    // Twenty hits on the far right edge of the paddle: the worst case, and far
    // more than it used to take to flatten the ball out.
    for (int hit = 0; hit < 20; ++hit) {
        Transform& at = game.ballAt();
        at.x = paddle.x + breakout::kPaddleWidth - 4.0f;
        at.y = breakout::kPaddleY - breakout::kBallRadius - 1.0f;
        ball.velocity = Vec2{ball.velocity.x, std::fabs(ball.velocity.y)};
        if (std::fabs(ball.velocity.y) < 1.0f) ball.velocity.y = 200.0f;

        game.driver.step(2);

        const float speed = std::hypot(ball.velocity.x, ball.velocity.y);
        const float verticalShare = std::fabs(ball.velocity.y) / speed;
        if (verticalShare < 0.25f) {
            check(false, "the ball keeps a usable vertical angle after "
                         "repeated edge hits");
            return;
        }
    }
    check(true, "the ball keeps a usable vertical angle after repeated edge hits");
}

void testLosingALife() {
    Game game;
    game.startPlaying();

    Ball& ball = game.ballData();
    ball.stuckToPaddle = false;
    ball.velocity = Vec2{0.0f, 900.0f};
    game.ballAt().y = static_cast<float>(breakout::kWindowHeight) - 20.0f;

    game.driver.step(10);

    check(game.session().lives == breakout::kStartLives - 1,
          "a ball that leaves the bottom costs a life");
    check(game.ballData().stuckToPaddle,
          "the next ball waits on the paddle again");
}

void testGameOverAndRestart() {
    Game game;
    game.startPlaying();

    game.session().lives = 1;
    game.session().score = 250;

    Ball& ball = game.ballData();
    ball.stuckToPaddle = false;
    ball.velocity = Vec2{0.0f, 900.0f};
    game.ballAt().y = static_cast<float>(breakout::kWindowHeight) - 20.0f;

    game.driver.step(10);
    check(game.session().gameOver, "the last life ends the game");

    // The overlay is on top now, so the paddle must not respond any more.
    Transform& paddle = *game.world.getComponent<Transform>(
        breakout::findPaddle(game.world));
    const float paddleX = paddle.x;
    game.driver.hold(SDL_SCANCODE_LEFT);
    game.driver.step(20);
    game.driver.release(SDL_SCANCODE_LEFT);
    check(std::fabs(paddle.x - paddleX) < 0.01f,
          "the game underneath the overlay is frozen");

    // R pops the overlay, and the play scene beneath resets itself.
    game.driver.tap(SDL_SCANCODE_R);
    game.driver.step(3);

    check(!game.session().gameOver, "R clears the game-over state");
    check(game.session().lives == breakout::kStartLives, "lives are restored");
    check(game.session().score == 0, "the score resets");
    check(breakout::countBricks(game.world) ==
              breakout::kBrickColumns * breakout::kBrickRows,
          "the field is refilled");
    check(game.ballData().stuckToPaddle, "the new ball waits on the paddle");
}

void testPauseFreezesPlay() {
    Game game;
    game.startPlaying();

    Ball& ball = game.ballData();
    ball.stuckToPaddle = false;
    ball.velocity = Vec2{0.0f, -200.0f};
    game.driver.step(5);

    game.driver.tap(SDL_SCANCODE_P);
    game.driver.step(2);

    const float frozenY = game.ballAt().y;
    game.driver.step(30);
    check(std::fabs(game.ballAt().y - frozenY) < 0.01f,
          "the ball does not move while paused");
    check(game.ball() != kInvalidEntity,
          "pausing keeps the field alive rather than unloading it");

    game.driver.tap(SDL_SCANCODE_P);
    game.driver.step(5);
    check(std::fabs(game.ballAt().y - frozenY) > 0.01f,
          "unpausing resumes exactly where it left off");
}

void testClearingTheFieldStartsANewLevel() {
    Game game;
    game.startPlaying();

    check(game.session().level == 1, "play starts on level one");

    for (Entity entity : game.world.entities()) {
        if (game.world.hasComponent<Brick>(entity)) {
            game.world.destroyLater(entity);
        }
    }
    game.driver.step(3);

    check(game.session().level == 2, "an empty field advances the level");
    check(breakout::countBricks(game.world) ==
              breakout::kBrickColumns * breakout::kBrickRows,
          "the next level refills the field");
    check(game.ballData().stuckToPaddle, "the new level starts with a fresh ball");
}

}  // namespace

int main() {
    std::printf("breakout tests\n");

    testStartingARound();
    testPaddleMovesAndIsClamped();
    testBallRidesPaddleUntilLaunched();
    testBallBreaksBricksAndScores();
    testFastBallDoesNotTunnel();
    testBallNeverFlattensOut();
    testLosingALife();
    testGameOverAndRestart();
    testPauseFreezesPlay();
    testClearingTheFieldStartsANewLevel();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
