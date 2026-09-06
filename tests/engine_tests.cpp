// ---------------------------------------------------------------------------
// engine_tests.cpp — asserts about the parts of the engine that are pure
// logic: collision maths, the tick timer, entity lifetime, the scene stack
// and the font table.
//
// There is no test framework here on purpose. A framework is another
// dependency to install and another API to learn, and what this needs is a
// list of things that must be true and a non-zero exit code when one isn't.
// `check(...)` is the whole harness.
//
// What is NOT covered: anything that needs a window or a GPU. Rendering is
// verified by looking at the screen. Everything below runs headless in
// milliseconds, which is what makes it worth running on every build.
//
// Run it with `ctest` from the build directory, or just run the binary.
// ---------------------------------------------------------------------------

// SDL.h #defines main to SDL_main so it can run its own startup before your
// code — helpful for a game, fatal for a console program, because the C
// runtime then can't find a main() to call and the link fails. This says
// "I'll handle main myself", and must come before any header that reaches
// SDL.h (Scene.h does, by way of Input.h).
#define SDL_MAIN_HANDLED

#include <cmath>
#include <cstdio>
#include <string>

#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Font.h"
#include "engine/Scene.h"
#include "engine/Systems.h"
#include "engine/Timing.h"

using namespace engine;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

bool nearly(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) < tolerance;
}

// --- Collision -------------------------------------------------------------

void testAabb() {
    const Collider box{10, 10};

    check(aabbOverlap(Transform{0.0f, 0.0f}, box, Transform{5.0f, 5.0f}, box),
          "overlapping boxes overlap");
    check(!aabbOverlap(Transform{0.0f, 0.0f}, box, Transform{50.0f, 0.0f}, box),
          "distant boxes do not overlap");
    check(!aabbOverlap(Transform{0.0f, 0.0f}, box, Transform{0.0f, 50.0f}, box),
          "boxes apart on y alone do not overlap");

    // The one that matters most: grid games place cells edge to edge, and if
    // touching counted as colliding, every snake segment would be permanently
    // inside its neighbour.
    check(!aabbOverlap(Transform{0.0f, 0.0f}, box, Transform{10.0f, 0.0f}, box),
          "boxes touching exactly at an edge do NOT overlap");
    check(aabbOverlap(Transform{0.0f, 0.0f}, box, Transform{9.9f, 0.0f}, box),
          "boxes overlapping by a sliver do overlap");
}

void testCircles() {
    const CircleCollider ten{10.0f};

    check(circleOverlap(Transform{0.0f, 0.0f}, ten, Transform{15.0f, 0.0f}, ten),
          "circles closer than the sum of radii overlap");
    check(!circleOverlap(Transform{0.0f, 0.0f}, ten, Transform{25.0f, 0.0f}, ten),
          "circles further apart than the sum of radii do not");
    check(!circleOverlap(Transform{0.0f, 0.0f}, ten, Transform{20.0f, 0.0f}, ten),
          "circles touching exactly do NOT overlap");

    // Diagonal separation: 3-4-5 triangle puts the centres 25 apart, which is
    // more than 10 + 10, even though each axis alone is less than 20.
    check(!circleOverlap(Transform{0.0f, 0.0f}, ten, Transform{15.0f, 20.0f}, ten),
          "circles use real distance, not per-axis distance");
}

void testCircleVsBox() {
    const CircleCollider circle{6.0f};
    const Collider box{20, 20};  // spans (100,100) to (120,120)
    const Transform boxAt{100.0f, 100.0f};

    check(circleAabbOverlap(Transform{110.0f, 110.0f}, circle, boxAt, box),
          "circle inside the box overlaps");
    check(circleAabbOverlap(Transform{97.0f, 110.0f}, circle, boxAt, box),
          "circle overlapping the left edge overlaps");
    check(!circleAabbOverlap(Transform{90.0f, 110.0f}, circle, boxAt, box),
          "circle clear of the box does not overlap");

    // Near a corner the closest point is the corner itself, so a circle can
    // be within range on both axes and still miss — the case a naive
    // "expand the box by the radius" test gets wrong.
    check(!circleAabbOverlap(Transform{95.0f, 95.0f}, circle, boxAt, box),
          "circle diagonally past a corner does not overlap");
    check(circleAabbOverlap(Transform{97.0f, 97.0f}, circle, boxAt, box),
          "circle close to a corner does overlap");
}

// The dispatch: same system, three shape combinations.
void testCollisionSystemDispatch() {
    World world;

    Entity boxEntity = world.createEntity();
    world.addComponent(boxEntity, Transform{0.0f, 0.0f});
    world.addComponent(boxEntity, Collider{20, 20});

    Entity circleEntity = world.createEntity();
    world.addComponent(circleEntity, Transform{10.0f, 10.0f});
    world.addComponent(circleEntity, CircleCollider{5.0f});

    Entity farEntity = world.createEntity();
    world.addComponent(farEntity, Transform{500.0f, 500.0f});
    world.addComponent(farEntity, CircleCollider{5.0f});

    // No collider at all: present in the world, absent from collision.
    Entity ghost = world.createEntity();
    world.addComponent(ghost, Transform{5.0f, 5.0f});

    auto collisions = CollisionSystem(world);
    check(collisions.size() == 1, "exactly one pair collides");
    if (collisions.size() == 1) {
        const bool rightPair =
            (collisions[0].a == boxEntity && collisions[0].b == circleEntity) ||
            (collisions[0].a == circleEntity && collisions[0].b == boxEntity);
        check(rightPair, "the colliding pair is the box and the circle");
    }

    check(collides(world, boxEntity, circleEntity), "mixed box/circle collides");
    check(!collides(world, boxEntity, farEntity), "distant entities do not");
    check(!collides(world, ghost, boxEntity),
          "an entity with no collider never collides");
}

// --- Timing ----------------------------------------------------------------

void testTickTimer() {
    TickTimer timer(1.0f);

    check(timer.advance(0.5f) == 0, "no tick before the interval elapses");
    check(timer.advance(0.4f) == 0, "still no tick at 0.9 of the interval");
    check(timer.advance(0.2f) == 1, "one tick once the interval is passed");

    // The remainder must be kept: 1.1 seconds banked minus one 1.0 tick
    // leaves 0.1, so 0.9 more should fire the next one. Discarding the
    // remainder is what makes ticks drift late.
    check(timer.advance(0.9f) == 1, "leftover time is banked, not discarded");

    // A long frame owes several ticks at once, which is what keeps game speed
    // independent of frame rate.
    TickTimer slow(0.1f);
    check(slow.advance(0.35f) == 3, "a long frame yields several ticks");

    slow.reset();
    check(slow.advance(0.05f) == 0, "reset clears banked time");

    // A zero interval would otherwise loop forever inside advance().
    TickTimer degenerate(0.0f);
    check(degenerate.advance(1.0f) == 0, "a zero interval never ticks");
}

// --- ECS -------------------------------------------------------------------

void testWorld() {
    World world;

    Entity entity = world.createEntity();
    check(entity != kInvalidEntity, "a new entity is not the invalid id");

    world.addComponent(entity, Transform{1.0f, 2.0f});
    check(world.hasComponent<Transform>(entity), "component was added");
    check(!world.hasComponent<Velocity>(entity), "absent component reads absent");

    Transform* transform = world.getComponent<Transform>(entity);
    check(transform != nullptr && nearly(transform->x, 1.0f),
          "component values survive the round trip");

    check(world.getComponent<Velocity>(entity) == nullptr,
          "getComponent returns null for an absent component");

    world.destroyEntity(entity);
    check(!world.hasComponent<Transform>(entity),
          "destroying an entity removes its components");
}

void testDeferredDestruction() {
    World world;
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{});

    world.destroyLater(entity);
    check(world.hasComponent<Transform>(entity),
          "destroyLater does not delete immediately");

    world.flushDestroyed();
    check(!world.hasComponent<Transform>(entity),
          "flushDestroyed applies the queued deletion");

    // Queuing the same entity twice must not be a problem: two systems can
    // independently decide the same thing should die.
    Entity twice = world.createEntity();
    world.addComponent(twice, Transform{});
    world.destroyLater(twice);
    world.destroyLater(twice);
    world.flushDestroyed();
    check(!world.hasComponent<Transform>(twice), "double-queued destroy is safe");
}

// --- Systems ---------------------------------------------------------------

void testMovementSystem() {
    World world;

    Entity mover = world.createEntity();
    world.addComponent(mover, Transform{0.0f, 0.0f, 0.0f});
    world.addComponent(mover, Velocity{100.0f, -50.0f});
    world.addComponent(mover, AngularVelocity{2.0f});

    // An entity with a Transform but no motion must not move.
    Entity still = world.createEntity();
    world.addComponent(still, Transform{7.0f, 7.0f, 0.5f});

    MovementSystem(world, 0.5f);

    Transform* moved = world.getComponent<Transform>(mover);
    check(nearly(moved->x, 50.0f), "velocity is scaled by dt on x");
    check(nearly(moved->y, -25.0f), "velocity is scaled by dt on y");
    check(nearly(moved->rotation, 1.0f), "angular velocity is scaled by dt");

    Transform* unmoved = world.getComponent<Transform>(still);
    check(nearly(unmoved->x, 7.0f) && nearly(unmoved->rotation, 0.5f),
          "an entity with no Velocity stays put");
}

void testLifetimeSystem() {
    World world;

    Entity shortLived = world.createEntity();
    world.addComponent(shortLived, Lifetime{0.5f});

    Entity longLived = world.createEntity();
    world.addComponent(longLived, Lifetime{10.0f});

    LifetimeSystem(world, 0.3f);
    world.flushDestroyed();
    check(world.hasComponent<Lifetime>(shortLived),
          "an unexpired lifetime survives");

    LifetimeSystem(world, 0.3f);
    check(world.hasComponent<Lifetime>(shortLived),
          "expiry is deferred, not immediate");

    world.flushDestroyed();
    check(!world.hasComponent<Lifetime>(shortLived), "an expired entity is gone");
    check(world.hasComponent<Lifetime>(longLived), "other entities are untouched");
}

// --- Scenes ----------------------------------------------------------------

// Records what the stack called on it, so the ordering can be asserted.
class RecordingScene : public Scene {
public:
    RecordingScene(std::string* log, char id) : log_(log), id_(id) {}

    void onEnter(World&) override { log_->push_back(id_); log_->push_back('+'); }
    void onExit(World&) override { log_->push_back(id_); log_->push_back('-'); }
    void onResume(World&) override { log_->push_back(id_); log_->push_back('^'); }

    void update(World&, InputManager&, float, SceneStack&) override {
        log_->push_back(id_);
        log_->push_back('u');
    }

private:
    std::string* log_;
    char id_;
};

void testSceneStack() {
    World world;
    InputManager input;
    std::string log;
    SceneStack scenes;

    check(scenes.empty(), "a new stack is empty");

    scenes.push(std::make_unique<RecordingScene>(&log, 'A'));
    check(log.empty(), "push only queues; nothing runs yet");
    check(scenes.empty(), "the stack is still empty before applyPending");

    scenes.applyPending(world);
    check(log == "A+", "applyPending enters the pushed scene");
    check(!scenes.empty(), "the stack now holds a scene");

    log.clear();
    scenes.update(world, input, 0.016f);
    check(log == "Au", "the top scene updates");

    // Push a second scene: the first must NOT exit, so a paused game keeps
    // its entities, and must NOT update while covered.
    log.clear();
    scenes.push(std::make_unique<RecordingScene>(&log, 'B'));
    scenes.applyPending(world);
    check(log == "B+", "pushing enters the new scene without exiting the old");

    log.clear();
    scenes.update(world, input, 0.016f);
    check(log == "Bu", "only the top scene updates");

    // Popping exits the top and resumes what was underneath.
    log.clear();
    scenes.pop();
    scenes.applyPending(world);
    check(log == "B-A^", "pop exits the top, then resumes the one below");

    // Replace swaps the top out for good: exit, then enter, no resume.
    log.clear();
    scenes.replace(std::make_unique<RecordingScene>(&log, 'C'));
    scenes.applyPending(world);
    check(log == "A-C+", "replace exits the old scene and enters the new one");

    log.clear();
    scenes.pop();
    scenes.applyPending(world);
    check(log == "C-", "popping the last scene exits it");
    check(scenes.empty(), "the stack is empty again, which the engine reads as quit");
}

// --- Font ------------------------------------------------------------------

void testFont() {
    for (const Glyph& glyph : kGlyphs) {
        const std::size_t expected =
            static_cast<std::size_t>(kGlyphWidth * kGlyphHeight);
        if (std::string(glyph.pixels).size() != expected) {
            std::printf("  FAIL: glyph '%c' is not %d pixels\n", glyph.character,
                        kGlyphWidth * kGlyphHeight);
            ++failures;
        }
        ++checks;
    }

    check(glyphFor('a') == glyphFor('A'), "lowercase folds to uppercase");
    check(glyphFor('\x01') == kMissingGlyph,
          "an unknown character falls back to the box glyph");

    // 'A' is drawn ".###." on its top row: not lit at the left edge, lit next.
    check(!glyphPixel(glyphFor('A'), 0, 0), "glyph pixel lookup reads column 0");
    check(glyphPixel(glyphFor('A'), 1, 0), "glyph pixel lookup reads column 1");

    // Three characters at scale 2: 3 * (5 + 1) * 2, minus the trailing gap.
    check(textWidth("ABC", 2) == 34, "text width accounts for spacing");
    check(textWidth("", 3) == 0, "an empty string has no width");
    check(textHeight(3) == kGlyphHeight * 3, "text height scales");
}

}  // namespace

int main() {
    std::printf("engine tests\n");

    testAabb();
    testCircles();
    testCircleVsBox();
    testCollisionSystemDispatch();
    testTickTimer();
    testWorld();
    testDeferredDestruction();
    testMovementSystem();
    testLifetimeSystem();
    testSceneStack();
    testFont();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
