#pragma once
// ---------------------------------------------------------------------------
// Harness.h — drives a SceneStack the way the engine does, but with no window,
// no GPU and no real time passing.
//
// Unit tests can check that `aabbContact` returns the right normal, but they
// can't tell you that losing your last life shows a game-over overlay, or that
// pressing R afterwards starts a clean round. Those live in the interaction
// between scenes, input and the world — and until now the only way to check
// them was to patch a game's main.cpp with synthetic key events, run it,
// eyeball the output, and revert. That got written and thrown away four times
// while this project was being built. This is that harness, kept.
//
// Two things make it trustworthy:
//
//   - It runs the same systems in the same order as Engine::run, and calls
//     the shared RunBuiltinSystems rather than repeating the list, so the two
//     cannot silently drift apart. Only rendering is missing, and rendering
//     changes no state.
//   - It feeds real SDL_Event structs into a real InputManager, so
//     wasKeyPressed() edge detection behaves exactly as it does in the game.
//     No SDL subsystem is initialised: an SDL_Event is just a struct.
//
// Time is a parameter, so a test can run "two seconds of play" in well under a
// millisecond, and every run is identical.
// ---------------------------------------------------------------------------

// SDL.h renames main() unless told otherwise, which breaks a console test
// binary. Must come before SDL.h is reached.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include <SDL.h>

#include <unordered_set>
#include <vector>

#include "engine/ECS.h"
#include "engine/Input.h"
#include "engine/Scene.h"
#include "engine/Systems.h"

namespace harness {

class Harness {
public:
    Harness(engine::World& world, engine::SceneStack& scenes)
        : world_(world), scenes_(scenes) {
        // Whatever was pushed before the first step is still only queued.
        scenes_.applyPending(world_);
    }

    // Down for exactly one frame, then released — a menu keypress.
    void tap(SDL_Scancode key) { taps_.push_back(key); }

    // Held until released — steering, thrust, a paddle.
    void hold(SDL_Scancode key) { held_.insert(key); }
    void release(SDL_Scancode key) { held_.erase(key); }

    void step(int frames = 1, float dt = 1.0f / 60.0f) {
        for (int frame = 0; frame < frames; ++frame) stepOnce(dt);
    }

    engine::InputManager& input() { return input_; }

private:
    void stepOnce(float dt) {
        deliverInput();

        // The same order as Engine::run, minus the drawing — including
        // letting a paused scene switch the built-in systems off.
        if (scenes_.simulating()) engine::RunBuiltinSystems(world_, dt);
        scenes_.update(world_, input_, dt);
        scenes_.applyPending(world_);
        world_.flushDestroyed();
    }

    // Turns the desired key state into the key up/down events SDL would have
    // produced, so InputManager sees genuine edges rather than being poked.
    void deliverInput() {
        input_.beginFrame();

        std::unordered_set<SDL_Scancode> wanted = held_;
        for (SDL_Scancode key : taps_) wanted.insert(key);
        taps_.clear();

        for (SDL_Scancode key : wanted) {
            if (down_.find(key) == down_.end()) send(key, SDL_KEYDOWN);
        }
        for (SDL_Scancode key : down_) {
            if (wanted.find(key) == wanted.end()) send(key, SDL_KEYUP);
        }
        down_ = wanted;
    }

    void send(SDL_Scancode key, Uint32 type) {
        SDL_Event event{};
        event.type = type;
        event.key.keysym.scancode = key;
        event.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
        input_.handleEvent(event);
    }

    engine::World& world_;
    engine::SceneStack& scenes_;
    engine::InputManager input_;

    std::unordered_set<SDL_Scancode> held_;
    std::unordered_set<SDL_Scancode> down_;
    std::vector<SDL_Scancode> taps_;
};

}  // namespace harness
