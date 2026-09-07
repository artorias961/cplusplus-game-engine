// Before ANY include: the engine headers reach SDL.h, which renames main().
#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// engine_bench.cpp — how much do the known-naive parts actually cost?
//
// The roadmap has had "packed component storage" and "a collision broad phase"
// on it for a long time, both filed under *optimise when something is slow*.
// The trouble with that rule is that nobody ever knows whether something is
// slow, so the items sit there forever collecting opinions.
//
// This measures them instead. It is not a test — there is nothing to pass or
// fail, so it is not registered with ctest. Run it when you want to know
// whether an optimisation is worth doing yet, and at what size it starts to
// matter.
//
// Read the numbers against a frame budget: 60fps is 16.6ms for EVERYTHING, so
// a system costing more than about 1ms is taking a real bite.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Systems.h"

using namespace engine;

namespace {

double millisecondsFor(int repeats, void (*body)(World&), World& world) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) body(world);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const double totalMs =
        std::chrono::duration<double, std::milli>(elapsed).count();
    return totalMs / repeats;
}

void runCollision(World& world) {
    volatile auto pairs = CollisionSystem(world).size();
    (void)pairs;
}

void runMovement(World& world) { MovementSystem(world, 1.0f / 60.0f); }

// The other O(n²) shape, and the one the fourth game actually runs.
//
// A lane battler does not use CollisionSystem at all: it asks "which enemy
// ahead of me is nearest?", which is a subtraction and a comparison rather
// than any shape maths. Same quadratic growth, a far smaller constant — and
// for a long time this benchmark could not say how much smaller, so answering
// "does the targeting scan need a spatial grid yet?" meant writing a
// throwaway probe and deleting it. It is measured here now, because the whole
// point of this file is that the optimise-later list gets decided by numbers.
void runNearestScan(World& world) {
    for (auto& [self, selfTransform] : world.view<Transform>()) {
        Entity best = kInvalidEntity;
        float bestGap = 1.0e9f;

        for (auto& [other, otherTransform] : world.view<Transform>()) {
            if (other == self) continue;
            const float gap = otherTransform.x - selfTransform.x;
            if (gap < 0.0f || gap > bestGap) continue;
            bestGap = gap;
            best = other;
        }
        volatile Entity keep = best;
        (void)keep;
    }
}

// A field of scattered movers, the shape both games actually produce.
void fill(World& world, int count) {
    std::mt19937 rng{1234u};
    std::uniform_real_distribution<float> position(0.0f, 2000.0f);

    for (int i = 0; i < count; ++i) {
        Entity entity = world.createEntity();
        world.addComponent(entity, Transform{position(rng), position(rng), 0.0f});
        world.addComponent(entity, Velocity{10.0f, 10.0f});
        world.addComponent(entity, CircleCollider{12.0f});
    }
}

void report(int count) {
    World world;
    fill(world, count);

    // Enough repeats that the timer resolution stops mattering.
    const int repeats = count > 400 ? 50 : 400;

    const double collisionMs = millisecondsFor(repeats, &runCollision, world);
    const double movementMs = millisecondsFor(repeats, &runMovement, world);
    const double scanMs = millisecondsFor(repeats, &runNearestScan, world);

    std::printf("%6d | %10.3f | %9.3f | %8.3f | %6.1f%%\n", count, collisionMs,
                movementMs, scanMs, 100.0 * (collisionMs + movementMs) / 16.6);
}

}  // namespace

int main() {
    std::printf("entities | collision ms | movement ms | scan ms | collision+movement\n");
    std::printf("---------+--------------+-------------+---------+-------------------\n");

    for (int count : {25, 50, 100, 200, 400, 800, 1600}) {
        report(count);
    }

    std::printf(
        "\nCollisionSystem compares every pair, so its cost grows with the\n"
        "SQUARE of the entity count: double the entities, quadruple the work.\n"
        "Find the row where it stops being free, and compare that against how\n"
        "many entities your game actually has.\n"
        "\n"
        "The `scan` column is the same quadratic shape without the shape maths:\n"
        "for each entity, walk them all and keep the nearest one ahead. That is\n"
        "what a lane battler runs instead of collision, and the gap between the\n"
        "two columns is the price of asking about overlap rather than distance.\n"
        "\n"
        "Lane Battle peaks around 140 entities, of which about 16 are units.\n"
        "Read the row nearest your own count before believing anything needs a\n"
        "spatial grid.\n");
    return 0;
}
