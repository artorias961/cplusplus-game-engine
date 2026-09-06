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

// The systems the engine runs for every game, in the order it runs them.
// Engine::run calls this, and so does anything driving the world without a
// window — a test, a headless replay — so the two can't drift apart.
inline void RunBuiltinSystems(World& world, float dt) {
    MovementSystem(world, dt);
    LifetimeSystem(world, dt);
}

}  // namespace engine
