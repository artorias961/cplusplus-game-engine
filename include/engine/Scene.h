#pragma once
// ---------------------------------------------------------------------------
// Scene.h — a stack of game states: menu, playing, paused, game over.
//
// Without this, "what mode is the game in?" turns into a pile of booleans in
// game code — gameOver, paused, showingMenu — and every one of them has to be
// checked in every update, in the right order, and reset in the right places.
// Four booleans are sixteen combinations, most of which are nonsense.
//
// A scene owns one mode. The engine updates the TOP scene only, so a mode is
// active exactly when it's on top, and the impossible combinations can't be
// expressed at all.
//
// Why a stack and not a single "current scene"? Because pausing needs the
// game underneath to still exist. Push the pause scene and the playing scene
// stops updating but keeps all its entities, so the frozen board still draws
// behind the overlay; pop it and everything resumes exactly where it was. A
// single current-scene variable would have to destroy and rebuild the level.
//
// The subtle part is that transitions are QUEUED, not applied immediately.
// A scene asking to be popped is running inside its own update() — deleting
// it there would destroy the object mid-call, out from under the `this`
// pointer it is executing on. So requests are recorded and applied later, by
// the engine, at a safe point. It's the same problem, and the same fix, as
// World::destroyLater.
// ---------------------------------------------------------------------------

#include <memory>
#include <utility>
#include <vector>

#include "engine/ECS.h"
#include "engine/Input.h"

namespace engine {

class SceneStack;  // Scenes request transitions on it; defined below.

class Scene {
public:
    virtual ~Scene() = default;

    // Called once when the scene is added to the stack. Build the entities
    // this scene owns here.
    virtual void onEnter(World& /*world*/) {}

    // Called once when the scene is removed. Destroy what onEnter built —
    // whatever a scene creates, it cleans up.
    virtual void onExit(World& /*world*/) {}

    // Called when the scene above this one is popped, making this the top
    // scene again. This is where "unpause" or "start a fresh round after the
    // game-over overlay closes" belongs.
    virtual void onResume(World& /*world*/) {}

    // Called once per frame, for the top scene only.
    virtual void update(World& world, InputManager& input, float dt,
                        SceneStack& scenes) = 0;

    // Should the engine's built-in systems run while this scene is on top?
    //
    // Only updates stop at a scene boundary; movement does not. Without this,
    // "paused" freezes the game's own logic while MovementSystem carries on
    // sliding every entity with a Velocity across the screen — which is
    // exactly what a pause is supposed to prevent. An overlay that means
    // "the world is holding still" returns false.
    virtual bool simulatesWorld() const { return true; }

    // Should Escape close the window while this scene is on top?
    //
    // Escape means "out of here", and for most of this project's games there
    // has only ever been one "here" to be out of, so the engine quitting on it
    // was right. A game with screens inside screens breaks that: Lane Battle's
    // army and hero screens print GO BACK next to the key, handle it in their
    // own update, and never got the chance — the engine had already stopped
    // the loop. The screen advertised a thing it could not do, and the player
    // who tried it lost the window.
    //
    // A scene that wants Escape for itself returns false. The default stays
    // true so the three games that never had sub-screens behave exactly as
    // they did, and closing the window is untouched either way: SDL_QUIT is
    // the operating system talking, not the player, and no scene may refuse
    // it.
    virtual bool escapeQuits() const { return true; }
};

using ScenePtr = std::unique_ptr<Scene>;

class SceneStack {
public:
    // All three only *queue* the change; nothing happens until the engine
    // calls applyPending. Calling them from inside update() is the normal
    // case and is safe.
    void push(ScenePtr scene) {
        pending_.push_back(Transition{Kind::Push, std::move(scene)});
    }

    void pop() { pending_.push_back(Transition{Kind::Pop, nullptr}); }

    // Swaps the top scene for a different one: the old scene exits for good
    // rather than waiting underneath. This is menu -> game; push is game ->
    // pause.
    void replace(ScenePtr scene) {
        pending_.push_back(Transition{Kind::Replace, std::move(scene)});
    }

    // Updates the top scene, and only the top scene. Everything below it is
    // frozen — though still rendered, because rendering is driven by the
    // components in the World, not by which scene put them there.
    void update(World& world, InputManager& input, float dt) {
        if (!scenes_.empty()) {
            scenes_.back()->update(world, input, dt, *this);
        }
    }

    // Applies queued transitions. The engine calls this after the update, at
    // a point where no scene's update() is on the call stack.
    void applyPending(World& world) {
        // Take the queue by value first: onEnter/onExit may queue further
        // transitions, and those belong to the NEXT flush, not this loop.
        // Appending to a vector being iterated would also invalidate it.
        std::vector<Transition> transitions = std::move(pending_);
        pending_.clear();

        for (Transition& transition : transitions) {
            switch (transition.kind) {
                case Kind::Push:
                    scenes_.push_back(std::move(transition.scene));
                    scenes_.back()->onEnter(world);
                    break;

                case Kind::Pop:
                    if (scenes_.empty()) break;
                    scenes_.back()->onExit(world);
                    scenes_.pop_back();
                    if (!scenes_.empty()) scenes_.back()->onResume(world);
                    break;

                case Kind::Replace:
                    if (!scenes_.empty()) {
                        scenes_.back()->onExit(world);
                        scenes_.pop_back();
                    }
                    scenes_.push_back(std::move(transition.scene));
                    scenes_.back()->onEnter(world);
                    break;
            }
        }
    }

    // An empty stack means there is no game left to run. Engine::run treats
    // that as "quit", which gives game code a natural way to end the program
    // without reaching for the Engine itself: pop the last scene.
    bool empty() const { return scenes_.empty(); }

    // Whether the top scene wants the world to keep moving. The engine asks
    // this before running its built-in systems each frame.
    bool simulating() const {
        return scenes_.empty() ? true : scenes_.back()->simulatesWorld();
    }

    bool escapeQuits() const {
        return scenes_.empty() ? true : scenes_.back()->escapeQuits();
    }

private:
    enum class Kind { Push, Pop, Replace };

    struct Transition {
        Kind kind;
        ScenePtr scene;  // null for Pop
    };

    std::vector<ScenePtr> scenes_;
    std::vector<Transition> pending_;
};

}  // namespace engine
