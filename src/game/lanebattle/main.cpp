// ---------------------------------------------------------------------------
// main.cpp — opens a window and hands control to Lane Battle's first scene.
//
// Same three-line shape as the other games: every rule lives in LaneBattle.cpp
// behind the interface in LaneBattle.h, so tests/lanebattle_tests.cpp can
// drive the same scenes without a window.
// ---------------------------------------------------------------------------

#include "LaneBattle.h"

#include "engine/ECS.h"
#include "engine/Engine.h"
#include "engine/Scene.h"

int main(int, char**) {
    engine::Engine gameEngine("Tiny Engine - Lane Battle",
                              lanebattle::kWindowWidth,
                              lanebattle::kWindowHeight);
    engine::World world;

    lanebattle::setAudioDevice(&gameEngine.audio());

    engine::SceneStack scenes;
    scenes.push(lanebattle::makeTitleScene());

    gameEngine.run(world, scenes);
    return 0;
}
