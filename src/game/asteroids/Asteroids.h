#pragma once
// ---------------------------------------------------------------------------
// Asteroids.h — the game's public surface.
//
// Asteroids began as a single main.cpp, which read well and could not be
// tested at all: every rule sat in an anonymous namespace behind a main() that
// opened a window. It now has the same shape as Breakout — a library plus a
// three-line main.cpp — so tests/asteroids_tests.cpp can drive exactly the
// scenes a player sees, with no window and no waiting.
//
// Only what a test or the entry point needs is exposed: tuning constants, the
// components worth looking for, a few queries, and the scene factories.
// ---------------------------------------------------------------------------

#include <memory>

#include "engine/Audio.h"
#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Scene.h"

namespace asteroids {

// --- Layout and tuning -----------------------------------------------------

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 720;

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = kPi * 2.0f;

// Thrust is an ACCELERATION, not a speed: holding Up adds to the ship's
// velocity rather than setting it, which is why the ship drifts and has to be
// flown rather than steered.
constexpr float kTurnSpeed = 3.4f;    // radians per second
constexpr float kThrust = 320.0f;     // pixels per second, per second
constexpr float kDrag = 0.45f;        // fraction of speed shed per second
constexpr float kMaxSpeed = 430.0f;
constexpr float kShipRadius = 12.0f;

constexpr float kBulletSpeed = 560.0f;
constexpr float kBulletLife = 1.05f;
constexpr float kFireCooldown = 0.22f;
constexpr float kBulletRadius = 2.5f;

// Rocks come in three sizes, indexed 2 (large) down to 0 (small). A large one
// breaks into two mediums, a medium into two smalls, a small into nothing.
constexpr int kLargeRock = 2;
constexpr float kRockRadius[3] = {14.0f, 26.0f, 46.0f};
constexpr int kRockScore[3] = {100, 50, 20};
constexpr float kRockSpeedMin[3] = {90.0f, 60.0f, 32.0f};
constexpr float kRockSpeedMax[3] = {190.0f, 130.0f, 85.0f};

constexpr int kStartLives = 3;
constexpr int kStartRocks = 4;
constexpr float kRespawnDelay = 1.4f;
constexpr float kSpawnProtection = 2.5f;
constexpr float kSafeSpawnDistance = 170.0f;
constexpr float kRespawnClearance = 70.0f;

// Draw layers. The field, then the heads-up display, then a dimming panel,
// then the text that sits on top of it.
constexpr int kFieldLayer = 0;
constexpr int kHudLayer = 5;
constexpr int kOverlayLayer = 10;
constexpr int kOverlayTextLayer = 11;

// --- Components ------------------------------------------------------------

struct Ship {};
struct Bullet {};
struct Debris {};

// Rocks carry one field rather than being a bare tag: the size index decides
// what it looks like, what it scores, and what it breaks into.
struct Rock {
    int size = kLargeRock;
};

// Anything that should reappear on the far side when it leaves the screen.
struct Wrapping {};

// A stand-in drawn at the opposite edge while its source straddles a seam, so
// a rock slides across the boundary instead of vanishing and popping back.
struct Ghost {
    engine::Entity source = engine::kInvalidEntity;
};

// The whole match's state, held as a component on one entity — the "singleton
// component" pattern. It puts the score somewhere anything holding a World&
// can find it, which is what lets a test read it without the scene exposing
// anything.
struct Session {
    int score = 0;
    int lives = kStartLives;
    int wave = 0;
    bool gameOver = false;

    // Counting down: how long until a new ship, and how long it stays
    // invulnerable once it arrives.
    float respawnDelay = 0.0f;
    float spawnProtection = 0.0f;

    // Screen shake left to play out, and how violent it is.
    float shakeSeconds = 0.0f;
    float shakeMagnitude = 0.0f;
};

// --- Queries ---------------------------------------------------------------

Session* findSession(engine::World& world);
engine::Entity findShip(engine::World& world);
int countRocks(engine::World& world);
int countBullets(engine::World& world);

// --- Wiring ----------------------------------------------------------------

// Optional. Given a sprite sheet the lives display and the title screen's
// tumbling rock use it; without one they fall back to plain rectangles.
// Given an audio device the game makes noise; without one it is silent.
void setSpriteSheet(SDL_Texture* sheet);
void setAudioDevice(engine::AudioDevice* audio);

// --- Scenes ----------------------------------------------------------------

std::unique_ptr<engine::Scene> makeTitleScene();
std::unique_ptr<engine::Scene> makePlayScene();

}  // namespace asteroids
