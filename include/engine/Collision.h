#pragma once
// ---------------------------------------------------------------------------
// Collision.h — which things overlap, and what it would take to separate them.
//
// Split out of Systems.h once collision outgrew it: movement and lifetimes are
// four lines each, while this is shapes, contact geometry and the pair search.
// Systems.h still includes this file, so code that only knows about Systems.h
// keeps working.
//
// The file is in two halves. The first answers "did it hit?" — the cheapest
// possible yes/no. The second answers "how do I respond?", returning a normal
// and a depth. Games needed only the first half until something had to bounce.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "engine/Components.h"
#include "engine/ECS.h"

namespace engine {

// --- Collision -------------------------------------------------------------
//
// Two entities collide when their collider shapes overlap. For rectangles,
// because they are always axis-aligned (a Collider never rotates),
// the test is four comparisons — the AABB ("axis-aligned bounding box")
// test, the cheapest useful collision check there is and the one nearly
// every 2D engine reaches for first.
//
// Notice what is deliberately missing: any notion of what a collision
// *means*. Losing a life, bouncing off a wall, picking up a coin — that is
// game logic, and it stays in game code. The engine's job ends at "these two
// rectangles overlap"; deciding what to do about it is somebody else's.

// The geometry of one overlap: whether it happened, and what it would take to
// undo it.
//
// `normal` is a unit vector pointing from A toward B, along the shortest way
// out. `depth` is how far they overlap along it. Move B by normal * depth (or
// A by the negative of it) and they are exactly touching.
//
// This is what turns detection into response. A yes/no answer is enough to
// die or to score, but anything that bounces needs to know *which way*: a ball
// hitting a brick's top must flip its vertical speed, and one hitting the side
// must flip its horizontal speed. That difference is the normal.
struct Contact {
    bool overlapping = false;
    Vec2 normal;
    float depth = 0.0f;
};

// One overlapping pair, with its contact geometry. Each pair is reported once:
// you get {a, b}, never also the mirrored {b, a} — so the normal always points
// from `a` to `b`.
struct CollisionPair {
    Entity a = kInvalidEntity;
    Entity b = kInvalidEntity;
    Vec2 normal;
    float depth = 0.0f;
};

// Reflects a direction off a surface with the given unit normal:
//
//     v' = v - 2 (v . n) n
//
// The dot product is the part of v heading into the surface; subtracting it
// twice removes that part and adds it back the other way, which is exactly
// what a mirror does. Speed is unchanged — only direction flips.
inline Vec2 reflect(Vec2 velocity, Vec2 normal) {
    const float dot = velocity.x * normal.x + velocity.y * normal.y;
    return Vec2{velocity.x - 2.0f * dot * normal.x,
                velocity.y - 2.0f * dot * normal.y};
}

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

// Circle against circle: the cheapest test of all. Two circles overlap when
// the distance between their centers is less than the sum of their radii, and
// comparing squared distances avoids a square root entirely.
//
// Both circles are centered on their Transform (see CircleCollider).
inline bool circleOverlap(const Transform& ta, const CircleCollider& ca,
                          const Transform& tb, const CircleCollider& cb) {
    const float dx = tb.x - ta.x;
    const float dy = tb.y - ta.y;
    const float radii = ca.radius + cb.radius;
    return (dx * dx + dy * dy) < (radii * radii);
}

// Circle against box, for entities that mix the two conventions.
//
// The trick: find the point on the rectangle closest to the circle's center
// by clamping that center into the rectangle's range on each axis. If that
// closest point is nearer than the radius, they overlap. Remember the box
// hangs down-right from its Transform while the circle is centered on its own.
inline bool circleAabbOverlap(const Transform& circleTransform,
                              const CircleCollider& circle,
                              const Transform& boxTransform,
                              const Collider& box) {
    const float left = boxTransform.x;
    const float top = boxTransform.y;
    const float right = left + static_cast<float>(box.width);
    const float bottom = top + static_cast<float>(box.height);

    const float closestX = std::max(left, std::min(circleTransform.x, right));
    const float closestY = std::max(top, std::min(circleTransform.y, bottom));

    const float dx = circleTransform.x - closestX;
    const float dy = circleTransform.y - closestY;
    return (dx * dx + dy * dy) < (circle.radius * circle.radius);
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
// --- Contacts: the overlap, plus the way out -------------------------------
//
// The three functions above answer "did it hit?". These answer "how do I
// respond?", and the maths is deliberately written out again rather than
// layered on top: the cheap query stays cheap, and each function reads on its
// own. Real engines make the same split, between an overlap test and manifold
// generation. Both versions are covered by the tests, so they cannot quietly
// drift apart.

// Box against box. The shortest way out of two overlapping rectangles is
// along whichever axis they overlap *least*, which is why this compares the
// two and keeps the smaller — pushing a ball out through the top of a brick
// it entered from the side would look wrong.
inline Contact aabbContact(const Transform& ta, const Collider& ca,
                           const Transform& tb, const Collider& cb) {
    const float halfAWidth = static_cast<float>(ca.width) * 0.5f;
    const float halfAHeight = static_cast<float>(ca.height) * 0.5f;
    const float halfBWidth = static_cast<float>(cb.width) * 0.5f;
    const float halfBHeight = static_cast<float>(cb.height) * 0.5f;

    // Boxes hang down-right from their Transform, so their centres are half a
    // width and height along.
    const float dx = (tb.x + halfBWidth) - (ta.x + halfAWidth);
    const float dy = (tb.y + halfBHeight) - (ta.y + halfAHeight);

    const float overlapX = (halfAWidth + halfBWidth) - std::fabs(dx);
    if (overlapX <= 0.0f) return Contact{};

    const float overlapY = (halfAHeight + halfBHeight) - std::fabs(dy);
    if (overlapY <= 0.0f) return Contact{};

    Contact contact;
    contact.overlapping = true;
    if (overlapX < overlapY) {
        contact.normal = Vec2{dx < 0.0f ? -1.0f : 1.0f, 0.0f};
        contact.depth = overlapX;
    } else {
        contact.normal = Vec2{0.0f, dy < 0.0f ? -1.0f : 1.0f};
        contact.depth = overlapY;
    }
    return contact;
}

// Circle against circle: the way out is straight along the line joining the
// centres, which makes this the simplest contact of the three.
inline Contact circleContact(const Transform& ta, const CircleCollider& ca,
                             const Transform& tb, const CircleCollider& cb) {
    const float dx = tb.x - ta.x;
    const float dy = tb.y - ta.y;
    const float radii = ca.radius + cb.radius;
    const float distanceSquared = dx * dx + dy * dy;

    if (distanceSquared >= radii * radii) return Contact{};

    Contact contact;
    contact.overlapping = true;

    const float distance = std::sqrt(distanceSquared);
    if (distance < 1e-6f) {
        // Perfectly concentric: every direction is equally short, so pick one
        // rather than dividing by zero.
        contact.normal = Vec2{1.0f, 0.0f};
        contact.depth = radii;
    } else {
        contact.normal = Vec2{dx / distance, dy / distance};
        contact.depth = radii - distance;
    }
    return contact;
}

// Circle against box, with the circle as A: the normal points from the circle
// toward the box.
inline Contact circleAabbContact(const Transform& circleTransform,
                                 const CircleCollider& circle,
                                 const Transform& boxTransform,
                                 const Collider& box) {
    const float left = boxTransform.x;
    const float top = boxTransform.y;
    const float right = left + static_cast<float>(box.width);
    const float bottom = top + static_cast<float>(box.height);

    const float closestX = std::max(left, std::min(circleTransform.x, right));
    const float closestY = std::max(top, std::min(circleTransform.y, bottom));

    // From the closest point on the box out to the circle's centre.
    const float dx = circleTransform.x - closestX;
    const float dy = circleTransform.y - closestY;
    const float distanceSquared = dx * dx + dy * dy;

    if (distanceSquared >= circle.radius * circle.radius) return Contact{};

    Contact contact;
    contact.overlapping = true;

    if (distanceSquared > 1e-12f) {
        const float distance = std::sqrt(distanceSquared);
        // Negated, because the normal runs from A (the circle) to B (the box).
        contact.normal = Vec2{-dx / distance, -dy / distance};
        contact.depth = circle.radius - distance;
        return contact;
    }

    // The circle's centre is inside the box, so there is no surface point to
    // push away from. Leave through whichever edge is nearest — which is what
    // stops a fast mover that ended up deep inside a wall from being ejected
    // through the far side of it.
    const float toLeft = circleTransform.x - left;
    const float toRight = right - circleTransform.x;
    const float toTop = circleTransform.y - top;
    const float toBottom = bottom - circleTransform.y;

    float shortest = toLeft;
    contact.normal = Vec2{1.0f, 0.0f};  // circle exits left, so A->B points right
    if (toRight < shortest) {
        shortest = toRight;
        contact.normal = Vec2{-1.0f, 0.0f};
    }
    if (toTop < shortest) {
        shortest = toTop;
        contact.normal = Vec2{0.0f, 1.0f};
    }
    if (toBottom < shortest) {
        shortest = toBottom;
        contact.normal = Vec2{0.0f, -1.0f};
    }
    contact.depth = shortest + circle.radius;
    return contact;
}

// Picks the right contact test for whatever shapes the two entities carry.
// The normal always points from `a` toward `b`.
inline Contact contactBetween(World& world, Entity a, Entity b) {
    const Transform& ta = *world.getComponent<Transform>(a);
    const Transform& tb = *world.getComponent<Transform>(b);

    Collider* boxA = world.getComponent<Collider>(a);
    Collider* boxB = world.getComponent<Collider>(b);
    CircleCollider* circleA = world.getComponent<CircleCollider>(a);
    CircleCollider* circleB = world.getComponent<CircleCollider>(b);

    if (boxA && boxB) return aabbContact(ta, *boxA, tb, *boxB);
    if (circleA && circleB) return circleContact(ta, *circleA, tb, *circleB);
    if (circleA && boxB) return circleAabbContact(ta, *circleA, tb, *boxB);

    if (boxA && circleB) {
        // Computed circle-first, so the normal comes back pointing from b to
        // a and has to be turned around.
        Contact contact = circleAabbContact(tb, *circleB, ta, *boxA);
        contact.normal = Vec2{-contact.normal.x, -contact.normal.y};
        return contact;
    }
    return Contact{};
}

// Do these two entities overlap, whatever shapes they happen to use? Picks
// the right test from the collider components each one carries. An entity
// with both a Collider and a CircleCollider is treated as a box; give an
// entity one shape or the other.
inline bool collides(World& world, Entity a, Entity b) {
    const Transform& ta = *world.getComponent<Transform>(a);
    const Transform& tb = *world.getComponent<Transform>(b);

    Collider* boxA = world.getComponent<Collider>(a);
    Collider* boxB = world.getComponent<Collider>(b);
    CircleCollider* circleA = world.getComponent<CircleCollider>(a);
    CircleCollider* circleB = world.getComponent<CircleCollider>(b);

    if (boxA && boxB) return aabbOverlap(ta, *boxA, tb, *boxB);
    if (circleA && circleB) return circleOverlap(ta, *circleA, tb, *circleB);
    if (circleA && boxB) return circleAabbOverlap(ta, *circleA, tb, *boxB);
    if (boxA && circleB) return circleAabbOverlap(tb, *circleB, ta, *boxA);
    return false;
}

inline std::vector<CollisionPair> CollisionSystem(World& world) {
    // A collider says how big, a Transform says where. An entity needs a
    // Transform and at least one collider shape to take part.
    std::vector<Entity> collidable;
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Transform>(entity)) continue;
        if (!world.hasComponent<Collider>(entity) &&
            !world.hasComponent<CircleCollider>(entity)) {
            continue;
        }
        collidable.push_back(entity);
    }

    std::vector<CollisionPair> collisions;
    for (std::size_t i = 0; i < collidable.size(); ++i) {
        // Starting j at i + 1 skips both self-pairs (i == j) and the
        // mirrored duplicates already covered by an earlier i.
        for (std::size_t j = i + 1; j < collidable.size(); ++j) {
            const Contact contact =
                contactBetween(world, collidable[i], collidable[j]);
            if (!contact.overlapping) continue;

            collisions.push_back(CollisionPair{collidable[i], collidable[j],
                                               contact.normal, contact.depth});
        }
    }
    return collisions;
}

}  // namespace engine
