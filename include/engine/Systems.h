#pragma once
// ---------------------------------------------------------------------------
// Systems.h — logic that operates on components.
//
// A "system" in ECS terms is just a function that loops over entities with
// a particular set of components and does something with them. Systems own
// no state of their own; all state lives in the World's components. That's
// what makes them easy to reorder, disable, or test in isolation.
//
// Collision used to live here too, and grew until it was five times the size
// of everything else combined. It now has its own header, which this one
// includes so that code written against Systems.h still compiles.
// ---------------------------------------------------------------------------

#include "engine/Collision.h"
#include "engine/Components.h"
#include "engine/ECS.h"

namespace engine {

// Moves and turns every entity, scaled by delta time so speed is in "per
// second" units regardless of frame rate. Position comes from Velocity and
// facing from AngularVelocity; an entity can have either, both, or neither.
inline void MovementSystem(World& world, float dt) {
    for (auto& [entity, transform] : world.view<Transform>()) {
        if (Velocity* vel = world.getComponent<Velocity>(entity)) {
            transform.x += vel->dx * dt;
            transform.y += vel->dy * dt;
        }
        if (AngularVelocity* spin = world.getComponent<AngularVelocity>(entity)) {
            transform.rotation += spin->radiansPerSecond * dt;
        }
    }
}

// Counts down every Lifetime and queues the expired ones for destruction.
//
// Note that it calls destroyLater rather than destroyEntity, from inside a
// loop over the Lifetime pool — which is precisely the situation that makes
// immediate destruction unsafe. This system is the reason destroyLater exists.
inline void LifetimeSystem(World& world, float dt) {
    for (auto& [entity, lifetime] : world.view<Lifetime>()) {
        lifetime.secondsRemaining -= dt;
        if (lifetime.secondsRemaining <= 0.0f) {
            world.destroyLater(entity);
        }
    }
}

// Advances every Animation and writes the frame it lands on into its Sprite.
//
// The loop that consumes `elapsed` is a `while` rather than an `if` on purpose,
// for the same reason TickTimer's is: a frame that ran long owes more than one
// step, and dropping the remainder makes an animation quietly run slow whenever
// the machine is busy. Banking it instead keeps a walk cycle the same length in
// wall-clock seconds however the frame rate wanders.
inline void AnimationSystem(World& world, float dt) {
    for (auto& [entity, animation] : world.view<Animation>()) {
        Sprite* sprite = world.getComponent<Sprite>(entity);
        if (!sprite) continue;  // an Animation with nothing to animate

        // Guarded rather than assumed: `frameCount` and `secondsPerFrame` are
        // the sort of numbers a data file supplies, and a zero interval would
        // spin the while-loop below forever.
        if (!animation.playing || animation.frameCount <= 1 ||
            animation.secondsPerFrame <= 0.0f) {
            continue;
        }

        animation.elapsed += dt;
        while (animation.elapsed >= animation.secondsPerFrame) {
            animation.elapsed -= animation.secondsPerFrame;
            ++animation.frame;

            if (animation.frame < animation.frameCount) continue;

            if (animation.loop) {
                animation.frame = 0;
            } else {
                // Held on the last frame rather than wrapping or vanishing: a
                // death animation should leave a corpse in its final pose, and
                // clearing `playing` is how the game knows it may now be
                // cleaned up.
                animation.frame = animation.frameCount - 1;
                animation.playing = false;
                animation.elapsed = 0.0f;
                break;
            }
        }

        const int width =
            animation.frameWidth > 0 ? animation.frameWidth : sprite->srcW;
        sprite->srcX = animation.frame * width;
    }
}

// The systems the engine runs for every game, in the order it runs them.
// Engine::run calls this, and so does anything driving the world without a
// window — a test, a headless replay — so the two can't drift apart.
inline void RunBuiltinSystems(World& world, float dt) {
    MovementSystem(world, dt);
    LifetimeSystem(world, dt);
    AnimationSystem(world, dt);
}

}  // namespace engine
