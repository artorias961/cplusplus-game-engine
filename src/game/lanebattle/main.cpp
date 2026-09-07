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
#include <iostream>

#include "engine/Scene.h"

int main(int, char**) {
    engine::Engine gameEngine("Tiny Engine - Lane Battle",
                              lanebattle::kWindowWidth,
                              lanebattle::kWindowHeight);
    engine::World world;

    // Balance comes from a file if there is one, and from the compiled-in
    // defaults if there is not. Said out loud on startup, because "why is this
    // unit still expensive" has exactly two answers and one of them is that
    // the file was never found.
    if (lanebattle::loadBalance(lanebattle::kBalancePath)) {
        std::cout << "Loaded roster from " << lanebattle::kBalancePath << " ("
                  << lanebattle::unitKindCount() << " unit types)\n";
    } else {
        std::cout << "No " << lanebattle::kBalancePath
                  << "; using the built-in roster.\n";
    }

    lanebattle::setAudioDevice(&gameEngine.audio());

    engine::SceneStack scenes;
    scenes.push(lanebattle::makeTitleScene());

    gameEngine.run(world, scenes);
    return 0;
}
