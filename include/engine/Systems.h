#pragma once
// ---------------------------------------------------------------------------
// Systems.h — logic that operates on components.
//
// A "system" in ECS terms is just a function that loops over entities with
// a particular set of components and does something with them. Systems own
// no state of their own; all state lives in the World's components. That's
// what makes them easy to reorder, disable, or test in isolation.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <vector>

#include "engine/ECS.h"
#include "engine/Components.h"

namespace engine {

// Moves every entity that has both a Transform and a Velocity, scaled by
// delta time so speed is in "pixels per second" regardless of frame rate.
inline void MovementSystem(World& world, float dt) {
    for (auto& [entity, transform] : world.view<Transform>()) {
        if (Velocity* vel = world.getComponent<Velocity>(entity)) {
            transform.x += vel->dx * dt;
            transform.y += vel->dy * dt;
        }
    }
}

// --- Collision -------------------------------------------------------------
//
// Two entities collide when their Collider rectangles overlap. Because those
// rectangles are always axis-aligned (this engine never rotates anything),
// the test is four comparisons — the AABB ("axis-aligned bounding box")
// test, the cheapest useful collision check there is and the one nearly
// every 2D engine reaches for first.
//
// Notice what is deliberately missing: any notion of what a collision
// *means*. Losing a life, bouncing off a wall, picking up a coin — that is
// game logic, and it stays in game code. The engine's job ends at "these two
// rectangles overlap"; deciding what to do about it is somebody else's.

// One overlapping pair. Each pair is reported once: you get {a, b}, never
// also the mirrored {b, a}.
struct CollisionPair {
    Entity a = kInvalidEntity;
    Entity b = kInvalidEntity;
};

// The AABB test itself, on raw components rather than entities, so it is
// usable for "would this box fit here?" questions about positions that no
// entity occupies yet.
//
// The comparisons are strict (`<`, not `<=`), so two rectangles that merely
// touch edge-to-edge do NOT count as overlapping. That matters for
// grid-aligned games, where neighboring cells share an edge by design.
inline bool aabbOverlap(const Transform& ta, const Collider& ca,
                        const Transform& tb, const Collider& cb) {
    const float aRight  = ta.x + static_cast<float>(ca.width);
    const float aBottom = ta.y + static_cast<float>(ca.height);
    const float bRight  = tb.x + static_cast<float>(cb.width);
    const float bBottom = tb.y + static_cast<float>(cb.height);

    // Overlapping on one axis means "A starts before B ends, and B starts
    // before A ends". Both axes have to overlap for the boxes to intersect;
    // if either one doesn't, there's a gap and we're done.
    return ta.x < bRight && tb.x < aRight &&
           ta.y < bBottom && tb.y < aBottom;
}

// Checks every collidable entity against every other one and returns the
// pairs that overlap.
//
// Unlike MovementSystem, the engine does NOT run this for you every frame.
// Its output is only useful to code that knows what the entities mean, so
// game code decides when to ask (a turn-based game might ask once per move
// rather than once per frame) and what the answer implies.
//
// This is the naive O(n^2) version: 100 collidables is ~5,000 checks, which
// is nothing at this scale. Real engines put a "broad phase" in front of it
// — a spatial grid or quadtree that rules out pairs too far apart to touch —
// so they never build the full pair list at all.
inline std::vector<CollisionPair> CollisionSystem(World& world) {
    // A Collider says how big, a Transform says where. An entity needs both
    // to take part; anything missing one is simply not collidable.
    std::vector<Entity> collidable;
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Collider>(entity)) continue;
        if (!world.hasComponent<Transform>(entity)) continue;
        collidable.push_back(entity);
    }

    std::vector<CollisionPair> collisions;
    for (std::size_t i = 0; i < collidable.size(); ++i) {
        const Entity a = collidable[i];
        const Transform& ta = *world.getComponent<Transform>(a);
        const Collider& ca = *world.getComponent<Collider>(a);

        // Starting j at i + 1 skips both self-pairs (i == j) and the
        // mirrored duplicates already covered by an earlier i.
        for (std::size_t j = i + 1; j < collidable.size(); ++j) {
            const Entity b = collidable[j];
            const Transform& tb = *world.getComponent<Transform>(b);
            const Collider& cb = *world.getComponent<Collider>(b);

            if (aabbOverlap(ta, ca, tb, cb)) {
                collisions.push_back(CollisionPair{a, b});
            }
        }
    }
    return collisions;
}

}  // namespace engine
