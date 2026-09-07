// ---------------------------------------------------------------------------
// main.cpp — opens a window and hands control to Breakout's first scene.
//
// Deliberately this short. Every rule lives in Breakout.cpp, behind the small
// interface in Breakout.h, which is what lets tests/breakout_tests.cpp drive
// exactly the same scenes with no window, no GPU and no waiting.
// ---------------------------------------------------------------------------

#include "Breakout.h"

#include "engine/ECS.h"
#include "engine/Engine.h"
#include "engine/Scene.h"

int main(int, char**) {
    engine::Engine gameEngine("Tiny Engine - Breakout", breakout::kWindowWidth,
                              breakout::kWindowHeight);
    engine::World world;

    // Optional: without a device the game is simply silent.
    breakout::setAudioDevice(&gameEngine.audio());

    engine::SceneStack scenes;
    scenes.push(breakout::makeTitleScene());

    gameEngine.run(world, scenes);
    return 0;
}
