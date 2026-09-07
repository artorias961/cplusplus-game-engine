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
// What is NOT covered here: rendering. That used to mean it was not covered
// anywhere, and was checked by looking at the screen; it now lives in
// render_tests.cpp, which draws real frames through SDL's "dummy" video driver
// and asserts on the pixels it reads back. Everything below runs headless in
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
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Font.h"
#include "engine/DataFile.h"
#include "engine/Input.h"
#include "engine/Scene.h"
#include "engine/Systems.h"
#include "engine/Timing.h"
#include "engine/View.h"

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

// --- Contacts: which way out, and how far ----------------------------------

void testAabbContact() {
    const Collider box{10, 10};

    // B sits mostly to the right of A and overlaps by 2 on x, 10 on y. The
    // shorter axis wins, so the way out is sideways.
    Contact side = aabbContact(Transform{0.0f, 0.0f}, box,
                               Transform{8.0f, 0.0f}, box);
    check(side.overlapping, "overlapping boxes report a contact");
    check(nearly(side.normal.x, 1.0f) && nearly(side.normal.y, 0.0f),
          "the normal points from A toward B, along x");
    check(nearly(side.depth, 2.0f), "depth is the overlap on the shorter axis");

    // Mirrored: B to the LEFT of A flips the normal.
    Contact left = aabbContact(Transform{0.0f, 0.0f}, box,
                               Transform{-8.0f, 0.0f}, box);
    check(nearly(left.normal.x, -1.0f), "the normal flips when B is left of A");

    // Overlapping less on y than x: the way out is vertical instead.
    Contact vertical = aabbContact(Transform{0.0f, 0.0f}, box,
                                   Transform{0.0f, 8.0f}, box);
    check(nearly(vertical.normal.y, 1.0f) && nearly(vertical.normal.x, 0.0f),
          "the shortest axis decides the direction, not the order of arguments");
    check(nearly(vertical.depth, 2.0f), "vertical depth is the y overlap");

    check(!aabbContact(Transform{0.0f, 0.0f}, box, Transform{10.0f, 0.0f}, box)
               .overlapping,
          "touching exactly is not a contact, matching aabbOverlap");
    check(!aabbContact(Transform{0.0f, 0.0f}, box, Transform{99.0f, 0.0f}, box)
               .overlapping,
          "distant boxes report no contact");
}

void testCircleContact() {
    const CircleCollider ten{10.0f};

    Contact contact = circleContact(Transform{0.0f, 0.0f}, ten,
                                    Transform{15.0f, 0.0f}, ten);
    check(contact.overlapping, "overlapping circles report a contact");
    check(nearly(contact.normal.x, 1.0f), "the normal runs A toward B");
    check(nearly(contact.depth, 5.0f), "depth is radii minus centre distance");

    // A 3-4-5 triangle scaled by 3: centres 15 apart, so 5 of overlap, and
    // the normal is the unit vector along it.
    Contact diagonal = circleContact(Transform{0.0f, 0.0f}, ten,
                                     Transform{9.0f, 12.0f}, ten);
    check(diagonal.overlapping, "diagonal overlap is detected");
    check(nearly(diagonal.normal.x, 0.6f) && nearly(diagonal.normal.y, 0.8f),
          "the diagonal normal is a unit vector");
    check(nearly(diagonal.depth, 5.0f), "diagonal depth uses real distance");

    // Concentric circles have no shortest way out; it must still not divide
    // by zero or hand back a NaN normal.
    Contact same = circleContact(Transform{5.0f, 5.0f}, ten,
                                 Transform{5.0f, 5.0f}, ten);
    check(same.overlapping, "concentric circles overlap");
    check(nearly(same.normal.x * same.normal.x + same.normal.y * same.normal.y,
                 1.0f),
          "concentric circles still produce a unit normal");
}

void testCircleAabbContact() {
    const CircleCollider circle{6.0f};
    const Collider box{20, 20};  // spans (100,100) to (120,120)
    const Transform boxAt{100.0f, 100.0f};

    // Circle overlapping the left edge: it should be pushed further left, so
    // the normal (circle toward box) points right.
    Contact leftEdge = circleAabbContact(Transform{97.0f, 110.0f}, circle,
                                         boxAt, box);
    check(leftEdge.overlapping, "circle overlapping the left edge contacts");
    check(nearly(leftEdge.normal.x, 1.0f) && nearly(leftEdge.normal.y, 0.0f),
          "the normal points from the circle into the box");
    check(nearly(leftEdge.depth, 3.0f), "depth is radius minus the gap");

    // Above the box: the way out is upward.
    Contact topEdge = circleAabbContact(Transform{110.0f, 96.0f}, circle, boxAt,
                                        box);
    check(nearly(topEdge.normal.y, 1.0f) && nearly(topEdge.normal.x, 0.0f),
          "a circle above the box is pushed up, not sideways");

    // Centre buried inside the box, nearest the top: it must leave through
    // the top rather than the far side.
    Contact inside = circleAabbContact(Transform{110.0f, 103.0f}, circle, boxAt,
                                       box);
    check(inside.overlapping, "a circle inside the box contacts");
    check(nearly(inside.normal.y, 1.0f),
          "a buried circle leaves through its nearest edge");
    check(inside.depth > 6.0f, "a buried circle's depth clears the surface");

    check(!circleAabbContact(Transform{50.0f, 50.0f}, circle, boxAt, box)
               .overlapping,
          "a distant circle reports no contact");
}

void testContactDispatchAndReflect() {
    World world;

    Entity box = world.createEntity();
    world.addComponent(box, Transform{0.0f, 0.0f});
    world.addComponent(box, Collider{20, 20});

    Entity ball = world.createEntity();
    world.addComponent(ball, Transform{10.0f, -4.0f});
    world.addComponent(ball, CircleCollider{6.0f});

    // Box first, circle second: the normal must still run from a to b, which
    // means the mixed case has to be flipped internally.
    Contact boxFirst = contactBetween(world, box, ball);
    Contact ballFirst = contactBetween(world, ball, box);
    check(boxFirst.overlapping && ballFirst.overlapping,
          "the pair contacts in both argument orders");
    check(nearly(boxFirst.normal.y, -1.0f),
          "box-then-circle points from the box toward the circle above it");
    check(nearly(ballFirst.normal.y, 1.0f),
          "circle-then-box points the opposite way");
    check(nearly(boxFirst.depth, ballFirst.depth),
          "depth does not depend on argument order");

    // Reflection: a ball falling straight down onto a floor bounces straight
    // up, and one arriving at an angle keeps its sideways speed.
    Vec2 straight = reflect(Vec2{0.0f, 100.0f}, Vec2{0.0f, -1.0f});
    check(nearly(straight.x, 0.0f) && nearly(straight.y, -100.0f),
          "a head-on bounce reverses exactly");

    Vec2 angled = reflect(Vec2{60.0f, 80.0f}, Vec2{0.0f, -1.0f});
    check(nearly(angled.x, 60.0f) && nearly(angled.y, -80.0f),
          "a glancing bounce keeps its tangential speed");

    Vec2 sideways = reflect(Vec2{-50.0f, 20.0f}, Vec2{1.0f, 0.0f});
    check(nearly(sideways.x, 50.0f) && nearly(sideways.y, 20.0f),
          "bouncing off a wall flips only the perpendicular part");
}

// CollisionSystem must report the same geometry the primitives do.
void testCollisionSystemReportsContacts() {
    World world;

    Entity a = world.createEntity();
    world.addComponent(a, Transform{0.0f, 0.0f});
    world.addComponent(a, Collider{10, 10});

    Entity b = world.createEntity();
    world.addComponent(b, Transform{8.0f, 0.0f});
    world.addComponent(b, Collider{10, 10});

    auto collisions = CollisionSystem(world);
    check(collisions.size() == 1, "one pair is reported");
    if (collisions.size() == 1) {
        check(nearly(collisions[0].depth, 2.0f),
              "the pair carries the penetration depth");
        check(nearly(collisions[0].normal.x, 1.0f),
              "the pair carries the contact normal");
    }
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

// A collider with no Transform has no position, so it cannot be anywhere and
// cannot hit anything. CollisionSystem filters those out before testing, but
// contactBetween is public and games call it directly — Breakout tests its
// ball against every entity carrying a Collider — so it has to survive the
// case rather than dereference a null pointer.
void testContactWithoutTransform() {
    World world;

    Entity solid = world.createEntity();
    world.addComponent(solid, Transform{0.0f, 0.0f});
    world.addComponent(solid, Collider{20, 20});

    Entity placeless = world.createEntity();
    world.addComponent(placeless, Collider{20, 20});  // no Transform

    check(!contactBetween(world, solid, placeless).overlapping,
          "an entity with no Transform reports no contact");
    check(!contactBetween(world, placeless, solid).overlapping,
          "and the same in the other argument order");
    check(!collides(world, solid, placeless),
          "collides() agrees rather than crashing");

    // It must also stay out of the system's results entirely.
    check(CollisionSystem(world).empty(),
          "CollisionSystem ignores a collider with nowhere to be");
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

// --- The view --------------------------------------------------------------
//
// These look almost too small to be worth writing, and for three games they
// would have been: no world was ever bigger than its window, so the camera sat
// at the origin and world coordinates and screen coordinates were the same
// number. A scrolling battlefield is the first thing that can tell the
// difference, and this rule is the whole of it.
//
// It is worth being clear about what these do and do not prove. They pin the
// arithmetic, which Engine::render now calls rather than repeating — so the
// two cannot drift. They cannot prove anything about what reaches the screen;
// that still needs a window and a pair of eyes.
void testViewTransform() {
    Camera camera{200.0f, 50.0f};

    // Moving the camera right slides the world left, which is the sign
    // convention it is easiest to get backwards.
    check(nearly(viewToScreenX(camera, 500.0f, false), 300.0f),
          "a world position is shifted by the negative of the camera");
    check(nearly(viewToScreenY(camera, 90.0f, false), 40.0f),
          "on both axes");

    // Screen-space things ignore the camera entirely: this is what keeps a
    // score in the corner of the screen rather than 200 pixels off the left of
    // it once the view has scrolled.
    check(nearly(viewToScreenX(camera, 500.0f, true), 500.0f),
          "screen-space x ignores the camera");
    check(nearly(viewToScreenY(camera, 90.0f, true), 90.0f),
          "screen-space y ignores the camera");

    // A camera at the origin is the identity, which is why the three games
    // written before any of this existed still draw exactly as they did.
    const Camera atOrigin;
    check(nearly(viewToScreenX(atOrigin, 137.0f, false), 137.0f),
          "with no camera, world space and screen space coincide");
    check(nearly(viewToScreenY(atOrigin, 137.0f, false), 137.0f),
          "on both axes");

    // Negative camera positions are not special-cased anywhere; a game may
    // legitimately want to look left of the origin.
    const Camera behind{-40.0f, 0.0f};
    check(nearly(viewToScreenX(behind, 10.0f, false), 50.0f),
          "a negative camera position shifts the world right");
}

// Between "moves with the world" and "ignores the camera" there is a third
// case the boolean could never express: scenery at a distance. The factor is
// the fraction of the camera's movement a thing gets.
void testParallax() {
    const Camera camera{1000.0f, 0.0f};

    check(nearly(viewToScreenX(camera, 500.0f, false, 1.0f), -500.0f),
          "parallax 1 is the world, exactly as before");
    check(nearly(viewToScreenX(camera, 500.0f, false, 0.5f), 0.0f),
          "parallax 0.5 slides past at half the rate");
    check(nearly(viewToScreenX(camera, 500.0f, false, 0.0f), 500.0f),
          "parallax 0 does not move at all");

    // Over 1 is a foreground that outruns the ground, which is the half of
    // depth a screenSpace flag could never reach.
    check(nearly(viewToScreenX(camera, 500.0f, false, 1.5f), -1000.0f),
          "parallax above 1 slides past faster than the ground");

    // Omitting the argument has to keep meaning what it always meant, or every
    // game written before this silently changes depth.
    check(nearly(viewToScreenX(camera, 500.0f, false),
                 viewToScreenX(camera, 500.0f, false, 1.0f)),
          "the default is the world, so existing games are untouched");

    // screenSpace wins. The two overlap arithmetically, and a HUD element that
    // someone also gave a parallax to must still be a HUD element.
    check(nearly(viewToScreenX(camera, 500.0f, true, 0.3f), 500.0f),
          "screenSpace overrides parallax rather than combining with it");

    check(nearly(scrollFactor(false, 0.25f), 0.25f), "the factor is the parallax");
    check(nearly(scrollFactor(true, 0.25f), 0.0f), "unless it is screen space");
}

void testViewRoundTrip() {
    const Camera camera{640.0f, 12.0f};

    // The inverse arrived with mouse input: SDL reports a click in window
    // pixels, and the thing under it is up to a screen and a half away.
    check(nearly(screenToWorldX(camera, 100.0f), 740.0f),
          "a screen position maps back to the world under it");
    check(nearly(screenToWorldY(camera, 100.0f), 112.0f), "on both axes");

    // Exact inverses, which is the property worth pinning: get this wrong and
    // a click lands on whatever the camera offset happens to be away.
    for (float x = -500.0f; x <= 3000.0f; x += 371.0f) {
        check(nearly(screenToWorldX(camera, viewToScreenX(camera, x, false)), x),
              "world -> screen -> world is the identity");
    }
}

// The mouse is shaped exactly like the keyboard, and for the same reason: a
// button stays physically down for several frames, so a spawn button driven by
// "is it held?" would buy a unit every frame the finger rested on it.
void testMouseInput() {
    InputManager input;

    SDL_Event move{};
    move.type = SDL_MOUSEMOTION;
    move.motion.x = 120;
    move.motion.y = 44;

    input.beginFrame();
    input.handleEvent(move);
    check(input.mouseX() == 120 && input.mouseY() == 44,
          "motion updates the reported position");
    check(!input.isMouseDown(), "and moving is not clicking");

    SDL_Event down{};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 200;
    down.button.y = 50;

    input.beginFrame();
    input.handleEvent(down);
    check(input.isMouseDown(), "a press is held");
    check(input.wasMousePressed(), "and registers as an edge on this frame");
    check(!input.wasMouseReleased(), "and is not a release");

    // A button event carries its own position, and it is the one that counts:
    // a fast click can arrive with no motion event before it.
    check(input.mouseX() == 200 && input.mouseY() == 50,
          "a click reports where it happened, not where the cursor last moved");

    // Still down next frame, but no longer an edge — the whole point.
    input.beginFrame();
    check(input.isMouseDown(), "it stays held across frames");
    check(!input.wasMousePressed(),
          "but only counts as pressed on the frame it went down");

    SDL_Event up{};
    up.type = SDL_MOUSEBUTTONUP;
    up.button.button = SDL_BUTTON_LEFT;
    up.button.x = 200;
    up.button.y = 50;

    input.beginFrame();
    input.handleEvent(up);
    check(!input.isMouseDown(), "releasing clears the held state");
    check(input.wasMouseReleased(), "and registers as a release edge");

    // Buttons are independent of one another.
    SDL_Event rightDown{};
    rightDown.type = SDL_MOUSEBUTTONDOWN;
    rightDown.button.button = SDL_BUTTON_RIGHT;
    input.beginFrame();
    input.handleEvent(rightDown);
    check(input.isMouseDown(SDL_BUTTON_RIGHT), "the right button is tracked");
    check(!input.isMouseDown(SDL_BUTTON_LEFT),
          "and does not affect the left one");
}

// --- Data files ------------------------------------------------------------
//
// The engine could load PNGs and nothing else, so every number that balanced a
// game was a constexpr and changing one cost a recompile.
//
// These write real files and read them back, rather than testing a parser
// against a string, because the failure modes worth catching are about files:
// a missing one, a truncated one, a value that is not a number.

// Writes `contents` to a temporary file and returns its path. Absolute, so
// DataFile's executable-relative resolution leaves it alone.
std::string writeTempFile(const char* name, const char* contents) {
    // Written next to the test binary rather than into a temp directory: it
    // needs no environment variable, it is the same place DataFile resolves
    // relative paths against, and it is cleaned up with the build.
    std::string path = name;
    if (char* base = SDL_GetBasePath()) {
        path = std::string(base) + name;
        SDL_free(base);
    }

    std::ofstream out(path);
    out << contents;
    out.close();
    return path;
}

void testDataFileParsing() {
    const std::string path = writeTempFile("engine_datafile_test.txt", R"(
# a comment, and a blank line above

[unit]
name = SOLDIER
cost = 60
health = 110.5

[unit]
name  =  ARCHER
cost=95
speed = not-a-number

[tuning]
gold_per_second = 14
)");

    DataFile file;
    check(file.load(path), "a file that exists loads");
    check(file.sectionCount() == 3, "three sections were found");

    const std::vector<const DataSection*> units = file.all("unit");
    check(units.size() == 2, "repeated sections are all kept, in order");
    check(units[0]->text("name", "?") == "SOLDIER", "the first is the first");
    check(units[1]->text("name", "?") == "ARCHER", "and the second the second");

    check(nearly(units[0]->number("cost", 0.0f), 60.0f), "integers parse");
    check(nearly(units[0]->number("health", 0.0f), 110.5f), "so do decimals");

    // Whitespace around keys, values and the equals sign is all noise.
    check(nearly(units[1]->number("cost", 0.0f), 95.0f),
          "no spaces around the equals is fine");
    check(units[1]->text("name", "?") == "ARCHER",
          "and extra spaces are trimmed off both sides");

    // The three ways a lookup can fail all land on the fallback rather than
    // throwing, which is what lets a partial file be useful.
    check(nearly(units[0]->number("speed", 42.0f), 42.0f),
          "a missing key falls back");
    check(nearly(units[1]->number("speed", 42.0f), 42.0f),
          "and so does a value that is not a number");
    check(units[0]->text("nothing", "fallback") == "fallback",
          "and a missing string falls back too");

    check(file.first("tuning") != nullptr, "a section can be looked up by name");
    check(file.first("nonexistent") == nullptr,
          "and one that is not there is null, not a crash");
    check(units[0]->has("cost") && !units[0]->has("speed"),
          "has() reports what is actually present");
}

// A missing file is the normal case for a game run from a bare build
// directory, and it must leave the caller's defaults untouched rather than
// zeroing them.
void testMissingDataFileIsNotFatal() {
    DataFile file;
    check(!file.load("definitely/not/a/real/path/units.txt"),
          "a missing file reports failure");
    check(file.sectionCount() == 0, "and yields nothing");
    check(file.first("unit") == nullptr, "with no sections to find");

    const DataSection* absent = file.first("unit");
    check(absent == nullptr, "so every lookup is a null check away from safe");
}

void testDataFileEdgeCases() {
    const std::string path = writeTempFile("engine_datafile_odd.txt", R"(
orphan = 7
[empty]
[values]
# just a comment
trailing = 12   # comment after a value
labelled = SOLDIER # the front line
= 5
noequals
)");

    DataFile file;
    check(file.load(path), "an odd file still loads");

    // A key before any [section] would otherwise be dropped in silence.
    const DataSection* orphans = file.first("");
    check(orphans != nullptr && nearly(orphans->number("orphan", 0.0f), 7.0f),
          "a key before any section still lands somewhere");

    const DataSection* empty = file.first("empty");
    check(empty != nullptr && !empty->has("anything"),
          "a section with no keys is a section with no keys");

    const DataSection* values = file.first("values");
    check(values != nullptr, "the section after it is still found");
    check(nearly(values->number("trailing", 0.0f), 12.0f),
          "a comment after a value does not become part of it");

    // Numbers hide this bug: strtof stops at the '#' whether or not the
    // comment was stripped, so a broken stripper still parses 12 correctly.
    // Strings are where it shows, and a name is a string — a roster row
    // reading `name = SOLDIER # the front line` would otherwise define a unit
    // called "SOLDIER # the front line" and silently stop matching.
    check(values->text("labelled", "?") == "SOLDIER",
          "a comment after a STRING value is stripped too");
    check(!values->has(""), "a line with no key on the left is ignored");
    check(!values->has("noequals"), "and a line with no equals sign is too");
}

int main() {
    std::printf("engine tests\n");

    testAabb();
    testCircles();
    testCircleVsBox();
    testAabbContact();
    testCircleContact();
    testCircleAabbContact();
    testContactDispatchAndReflect();
    testCollisionSystemReportsContacts();
    testCollisionSystemDispatch();
    testContactWithoutTransform();
    testTickTimer();
    testWorld();
    testDeferredDestruction();
    testMovementSystem();
    testLifetimeSystem();
    testSceneStack();
    testFont();
    testViewTransform();
    testParallax();
    testViewRoundTrip();
    testMouseInput();
    testDataFileParsing();
    testMissingDataFileIsNotFatal();
    testDataFileEdgeCases();

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
