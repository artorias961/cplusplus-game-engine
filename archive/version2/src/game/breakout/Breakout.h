#pragma once
// ---------------------------------------------------------------------------
// Breakout.h — the game's public surface.
//
// Asteroids is a single main.cpp, which means none of its logic can be reached
// from a test: everything lives in an anonymous namespace behind a main() that
// opens a window. Breakout is split instead into a library (this header plus
// Breakout.cpp) and a three-line main.cpp, so the same scenes the player sees
// can be driven headlessly by tests/breakout_tests.cpp.
//
// What's exposed is deliberately small: the tuning constants, the components a
// test needs to look for, and factory functions for the scenes. The rules
// themselves stay private to the .cpp.
// ---------------------------------------------------------------------------

#include <memory>

#include "engine/Audio.h"
#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Scene.h"

namespace breakout {

// --- Layout ----------------------------------------------------------------

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 700;
constexpr int kWallThickness = 12;
constexpr int kFieldMargin = 20;

constexpr int kBrickColumns = 10;
constexpr int kBrickRows = 6;
constexpr int kBrickGap = 6;
constexpr int kBrickTop = 96;
constexpr int kBrickHeight = 24;

constexpr float kPaddleWidth = 110.0f;
constexpr float kPaddleHeight = 16.0f;
constexpr float kPaddleY = 620.0f;
constexpr float kPaddleSpeed = 640.0f;

constexpr float kBallRadius = 8.0f;
constexpr float kBallSpeed = 400.0f;
constexpr float kBallSpeedPerLevel = 45.0f;

// How far the ball may travel between collision checks. See Breakout.cpp for
// why this exists at all — it is the fix for a fast ball passing straight
// through a thin brick in a single frame.
constexpr float kMaxSubstep = 4.0f;

constexpr int kStartLives = 3;
constexpr int kBrickScore = 10;

// Brick width is derived so the rows always span the field exactly, whatever
// the column count and window size are set to.
inline float brickWidth() {
    const float span = static_cast<float>(kWindowWidth) -
                       2.0f * static_cast<float>(kWallThickness + kFieldMargin) -
                       static_cast<float>((kBrickColumns - 1) * kBrickGap);
    return span / static_cast<float>(kBrickColumns);
}

// --- Components ------------------------------------------------------------

struct Paddle {};

// The row a brick came from, used to pitch its break sound.
struct Brick {
    int row = 0;
};
struct Wall {};

// The ball keeps its own velocity instead of using the engine's Velocity
// component, because it must be moved in sub-frame steps rather than in one
// jump per frame. See moveBall() in Breakout.cpp.
struct Ball {
    engine::Vec2 velocity;
    bool stuckToPaddle = true;
};

// One entity holds the whole match's state. Storing it as a component rather
// than as a member of the scene is the "singleton component" pattern: it puts
// the score somewhere anything with a World& can find it, which is what makes
// it observable from a test without the scene exposing anything.
struct Session {
    int score = 0;
    int lives = kStartLives;
    int level = 1;
    bool gameOver = false;
};

// --- Queries (used by the game and by its tests) ---------------------------

Session* findSession(engine::World& world);
engine::Entity findBall(engine::World& world);
engine::Entity findPaddle(engine::World& world);
int countBricks(engine::World& world);

// Optional: with a device the game makes noise, without one it is silent.
void setAudioDevice(engine::AudioDevice* audio);

// --- Scenes ----------------------------------------------------------------

std::unique_ptr<engine::Scene> makeTitleScene();
std::unique_ptr<engine::Scene> makePlayScene();

}  // namespace breakout
