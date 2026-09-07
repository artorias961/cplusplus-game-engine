// ---------------------------------------------------------------------------
// main.cpp — opens a window and hands control to Asteroids' first scene.
//
// Everything else lives in Asteroids.cpp, behind the small interface in
// Asteroids.h, which is what lets tests/asteroids_tests.cpp drive the same
// scenes with no window.
// ---------------------------------------------------------------------------

#include "Asteroids.h"

#include "engine/ECS.h"
#include "engine/Engine.h"
#include "engine/Scene.h"

int main(int, char**) {
    engine::Engine gameEngine("Tiny Engine - Asteroids", asteroids::kWindowWidth,
                              asteroids::kWindowHeight);
    engine::World world;

    // Both are optional. Without the artwork the lives display falls back to
    // plain rectangles; without a sound device the game is simply silent.
    asteroids::setSpriteSheet(gameEngine.textures().load("assets/asteroids.png"));
    asteroids::setAudioDevice(&gameEngine.audio());

    engine::SceneStack scenes;
    scenes.push(asteroids::makeTitleScene());

    gameEngine.run(world, scenes);
    return 0;
}
