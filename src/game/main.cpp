// ---------------------------------------------------------------------------
// main.cpp — Asteroids, built on the engine in include/engine/.
//
// This is the engine's second game, and it deliberately exercises the half
// that Snake never touched:
//
//   - Continuous motion. Everything here has a Velocity and is moved by the
//     engine's MovementSystem, scaled by dt. Snake moved on a fixed tick and
//     never used Velocity at all.
//   - Rotation. The ship turns, rocks tumble, and the renderer transforms
//     every vertex accordingly (Transform::rotation, AngularVelocity).
//   - Circle collision. A box collider is wrong for a rotating ship, so
//     everything here uses CircleCollider — a different shape running through
//     the same CollisionSystem that served Snake's boxes.
//   - Entity churn. Bullets appear and expire, rocks split into smaller
//     rocks, and all of it is deleted mid-frame through destroyLater.
//
// It also uses both render paths on purpose: the ship, bullets and rocks are
// vector Polygons (no assets, rotate exactly), while the lives display and
// the title screen's tumbling rock are textured Sprites.
//
// As with Snake, none of this leaks into engine/. The engine has no idea what
// a rock is; it sees Transforms, Velocities, Polygons and colliders.
// ---------------------------------------------------------------------------

#include <SDL.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "engine/Scene.h"
#include "engine/Systems.h"

using namespace engine;

namespace {

// --- Tuning knobs ----------------------------------------------------------

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 720;

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = kPi * 2.0f;

// Ship handling. Thrust is an ACCELERATION, not a speed: holding Up adds to
// the ship's velocity rather than setting it, which is why the ship drifts
// and has to be flown rather than steered.
constexpr float kTurnSpeed = 3.4f;    // radians per second
constexpr float kThrust = 320.0f;     // pixels per second, per second
constexpr float kDrag = 0.45f;        // fraction of speed shed per second
constexpr float kMaxSpeed = 430.0f;
constexpr float kShipRadius = 12.0f;

constexpr float kBulletSpeed = 560.0f;
constexpr float kBulletLife = 1.05f;  // seconds before it expires on its own
constexpr float kFireCooldown = 0.22f;
constexpr float kBulletRadius = 2.5f;

// Rocks come in three sizes, indexed 2 (large) down to 0 (small). A large one
// breaks into two mediums, a medium into two smalls, a small into nothing.
constexpr int kLargeRock = 2;
constexpr float kRockRadius[3] = {14.0f, 26.0f, 46.0f};
constexpr int kRockScore[3] = {100, 50, 20};
constexpr float kRockSpeedMin[3] = {90.0f, 60.0f, 32.0f};
constexpr float kRockSpeedMax[3] = {190.0f, 130.0f, 85.0f};

constexpr int kStartLives = 3;
constexpr int kStartRocks = 4;
constexpr float kRespawnDelay = 1.4f;      // pause after dying
constexpr float kSpawnProtection = 2.5f;   // invulnerable seconds after that
constexpr float kSafeSpawnDistance = 170.0f;
// How much room a returning ship needs around the middle before it is safe
// to put it back. Ship radius plus a margin wide enough that a rock drifting
// past isn't already on top of you the moment you appear.
constexpr float kRespawnClearance = 70.0f;

// The sprite sheet: a ship icon at (0,0,32,32) and a rock at (32,0,64,64).
constexpr int kShipIconTile = 0;
SDL_Texture* spriteSheet = nullptr;

// Draw layers: the field, then overlay panels on top of it.
constexpr int kFieldLayer = 0;
constexpr int kOverlayLayer = 10;

// --- Game-specific components ----------------------------------------------

struct Ship {};
struct Bullet {};

// Rocks carry one field rather than being a bare tag: the size index decides
// what it looks like, what it scores, and what it breaks into.
struct Rock {
    int size = kLargeRock;
};

// --- Math helpers ----------------------------------------------------------

// A unit vector pointing along `radians`. Rotation 0 points right, and y
// grows downward, so this matches what the renderer does with the same angle.
Vec2 heading(float radians) {
    return Vec2{std::cos(radians), std::sin(radians)};
}

float randomRange(std::mt19937& rng, float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(rng);
}

// Space wraps: fly off one edge and you come back on the opposite one.
//
// This is the simple version, which teleports an object once its centre
// crosses the boundary — so a big rock briefly pops rather than sliding
// across the seam. Drawing everything a second time, offset by one screen
// width, is what fixes that; it costs a second set of entities or a second
// draw pass, and at this size it isn't worth it.
void wrapPosition(Transform& transform) {
    const float width = static_cast<float>(kWindowWidth);
    const float height = static_cast<float>(kWindowHeight);

    if (transform.x < 0.0f) transform.x += width;
    else if (transform.x >= width) transform.x -= width;

    if (transform.y < 0.0f) transform.y += height;
    else if (transform.y >= height) transform.y -= height;
}

// --- Shapes ----------------------------------------------------------------
//
// Polygon points are in local space, centred on the entity, so the renderer's
// rotate-then-translate puts them on screen facing the right way. At rotation
// 0 the ship points right, matching heading() above.

Polygon shipPolygon() {
    Polygon polygon;
    polygon.points = {
        Vec2{16.0f, 0.0f},     // nose
        Vec2{-11.0f, -10.0f},  // left wing
        Vec2{-6.0f, 0.0f},     // notch in the tail
        Vec2{-11.0f, 10.0f},   // right wing
    };
    polygon.r = 200;
    polygon.g = 230;
    polygon.b = 255;
    return polygon;
}

// The thrust flame, drawn as an open V behind the ship. It's a separate
// entity that follows the ship, switched on and off by setting its alpha —
// cheaper and simpler than creating and destroying it several times a second.
Polygon flamePolygon() {
    Polygon polygon;
    polygon.points = {Vec2{-7.0f, -5.0f}, Vec2{-19.0f, 0.0f}, Vec2{-7.0f, 5.0f}};
    polygon.closed = false;
    polygon.r = 255;
    polygon.g = 170;
    polygon.b = 60;
    polygon.a = 0;  // hidden until the player thrusts
    return polygon;
}

// Every rock gets its own lumpy outline: points spaced evenly around a circle
// and then pushed in or out at random. Two rocks are never quite alike, and
// it costs nothing but a few random numbers — the kind of thing vector
// graphics make easy and a sprite sheet makes tedious.
Polygon rockPolygon(std::mt19937& rng, float radius) {
    Polygon polygon;
    const int pointCount = static_cast<int>(randomRange(rng, 8.0f, 12.0f));

    for (int i = 0; i < pointCount; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) /
                            static_cast<float>(pointCount);
        const float distance = radius * randomRange(rng, 0.72f, 1.14f);
        polygon.points.push_back(
            Vec2{std::cos(angle) * distance, std::sin(angle) * distance});
    }

    polygon.r = 190;
    polygon.g = 190;
    polygon.b = 205;
    return polygon;
}

// A short dash pointing the way it travels — the classic look, and it keeps
// the bullet centred on its Transform the way its circle collider is.
Polygon bulletPolygon() {
    Polygon polygon;
    polygon.points = {Vec2{-3.0f, 0.0f}, Vec2{3.0f, 0.0f}};
    polygon.closed = false;
    polygon.r = 255;
    polygon.g = 240;
    polygon.b = 180;
    return polygon;
}

// --- Text helpers ----------------------------------------------------------

Entity createText(World& world, const std::string& value, int x, int y,
                  int scale, unsigned char r, unsigned char g,
                  unsigned char b) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{static_cast<float>(x),
                                         static_cast<float>(y), 0.0f});
    world.addComponent(entity, Text{value, scale, r, g, b, 255});
    return entity;
}

Entity createCenteredText(World& world, const std::string& value, int y,
                          int scale, unsigned char r, unsigned char g,
                          unsigned char b) {
    const int x = (kWindowWidth - textWidth(value, scale)) / 2;
    return createText(world, value, x, y, scale, r, g, b);
}

// The translucent panel that dims the field behind an overlay's text.
Entity createBackdrop(World& world) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{0.0f, 0.0f, 0.0f});

    Sprite panel;
    panel.width = kWindowWidth;
    panel.height = kWindowHeight;
    panel.r = 8;
    panel.g = 8;
    panel.b = 16;
    panel.a = 175;
    panel.layer = kOverlayLayer;
    world.addComponent(entity, panel);

    return entity;
}

// --- Game state ------------------------------------------------------------

struct GameState {
    Entity ship = kInvalidEntity;
    Entity flame = kInvalidEntity;

    int score = 0;
    int lives = kStartLives;
    int wave = 0;
    bool gameOver = false;

    float fireCooldown = 0.0f;
    float respawnDelay = 0.0f;      // counting down to a new ship
    float spawnProtection = 0.0f;   // invulnerable while this is positive

    std::mt19937 rng{std::random_device{}()};
};

int countRocks(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Rock>(entity)) ++count;
    }
    return count;
}

// Is the middle of the screen clear enough to put a ship back into?
//
// Without this check a new ship appears at the centre regardless of what is
// already there. Spawn protection hides the problem for a couple of seconds
// and then the ship dies the instant it wears off — and again on the next
// life, and the next, which can empty the whole stock of lives without the
// player ever touching a control. Rocks are always moving, so waiting for a
// gap always terminates.
bool spawnAreaClear(World& world) {
    const float centerX = static_cast<float>(kWindowWidth) / 2.0f;
    const float centerY = static_cast<float>(kWindowHeight) / 2.0f;

    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Rock>(entity)) continue;

        Transform* transform = world.getComponent<Transform>(entity);
        CircleCollider* collider = world.getComponent<CircleCollider>(entity);
        if (!transform || !collider) continue;

        const float distance = std::hypot(transform->x - centerX,
                                          transform->y - centerY);
        if (distance < collider->radius + kRespawnClearance) return false;
    }
    return true;
}

// --- Spawning --------------------------------------------------------------

void spawnShip(World& world, GameState& state) {
    Entity ship = world.createEntity();
    // -pi/2 is "up": rotation 0 points right and y grows downward.
    world.addComponent(ship, Transform{static_cast<float>(kWindowWidth) / 2.0f,
                                       static_cast<float>(kWindowHeight) / 2.0f,
                                       -kPi / 2.0f});
    world.addComponent(ship, Velocity{});
    world.addComponent(ship, shipPolygon());
    world.addComponent(ship, CircleCollider{kShipRadius});
    world.addComponent(ship, PlayerControlled{});
    world.addComponent(ship, Ship{});

    // The flame is its own entity that copies the ship's Transform each frame.
    Entity flame = world.createEntity();
    world.addComponent(flame, Transform{});
    world.addComponent(flame, flamePolygon());

    state.ship = ship;
    state.flame = flame;
    state.spawnProtection = kSpawnProtection;
}

Entity spawnRock(World& world, GameState& state, float x, float y, int size) {
    const float radius = kRockRadius[size];

    Entity rock = world.createEntity();
    world.addComponent(rock, Transform{x, y, randomRange(state.rng, 0.0f, kTwoPi)});

    const float angle = randomRange(state.rng, 0.0f, kTwoPi);
    const float speed = randomRange(state.rng, kRockSpeedMin[size],
                                    kRockSpeedMax[size]);
    world.addComponent(rock, Velocity{std::cos(angle) * speed,
                                      std::sin(angle) * speed});
    world.addComponent(rock, AngularVelocity{randomRange(state.rng, -1.5f, 1.5f)});
    world.addComponent(rock, rockPolygon(state.rng, radius));

    // A little smaller than the drawn outline, because the outline's points
    // stick out past the average radius and a hitbox that punishes near
    // misses feels unfair.
    world.addComponent(rock, CircleCollider{radius * 0.85f});
    world.addComponent(rock, Rock{size});

    return rock;
}

// Fills the field with large rocks, avoiding the middle so the player isn't
// killed the instant a wave begins.
void spawnWave(World& world, GameState& state) {
    ++state.wave;
    const int count = kStartRocks + state.wave - 1;

    const float centerX = static_cast<float>(kWindowWidth) / 2.0f;
    const float centerY = static_cast<float>(kWindowHeight) / 2.0f;

    for (int i = 0; i < count; ++i) {
        float x = 0.0f;
        float y = 0.0f;
        do {
            x = randomRange(state.rng, 0.0f, static_cast<float>(kWindowWidth));
            y = randomRange(state.rng, 0.0f, static_cast<float>(kWindowHeight));
        } while (std::hypot(x - centerX, y - centerY) < kSafeSpawnDistance);

        spawnRock(world, state, x, y, kLargeRock);
    }
}

void fireBullet(World& world, GameState& state) {
    Transform* shipTransform = world.getComponent<Transform>(state.ship);
    Velocity* shipVelocity = world.getComponent<Velocity>(state.ship);
    if (!shipTransform || !shipVelocity) return;

    const Vec2 direction = heading(shipTransform->rotation);

    Entity bullet = world.createEntity();
    world.addComponent(bullet, Transform{shipTransform->x + direction.x * 16.0f,
                                         shipTransform->y + direction.y * 16.0f,
                                         shipTransform->rotation});

    // The ship's own velocity is added in, so shots fired while flying fast
    // keep up with the ship instead of being left behind.
    world.addComponent(bullet,
                       Velocity{shipVelocity->dx + direction.x * kBulletSpeed,
                                shipVelocity->dy + direction.y * kBulletSpeed});
    world.addComponent(bullet, bulletPolygon());
    world.addComponent(bullet, CircleCollider{kBulletRadius});

    // No bookkeeping list of live bullets: LifetimeSystem deletes it.
    world.addComponent(bullet, Lifetime{kBulletLife});
    world.addComponent(bullet, Bullet{});

    state.fireCooldown = kFireCooldown;
}

// --- Per-frame logic -------------------------------------------------------

void controlShip(World& world, GameState& state, InputManager& input, float dt) {
    if (state.ship == kInvalidEntity) return;

    Transform* transform = world.getComponent<Transform>(state.ship);
    Velocity* velocity = world.getComponent<Velocity>(state.ship);
    if (!transform || !velocity) return;

    if (input.isKeyDown(SDL_SCANCODE_LEFT)) transform->rotation -= kTurnSpeed * dt;
    if (input.isKeyDown(SDL_SCANCODE_RIGHT)) transform->rotation += kTurnSpeed * dt;

    const bool thrusting = input.isKeyDown(SDL_SCANCODE_UP);
    if (thrusting) {
        const Vec2 direction = heading(transform->rotation);
        velocity->dx += direction.x * kThrust * dt;
        velocity->dy += direction.y * kThrust * dt;
    }

    // Drag, so the ship eventually coasts to a stop instead of drifting
    // forever. Multiplying by (1 - k*dt) rather than subtracting a fixed
    // amount keeps the slowdown proportional to the current speed.
    const float damping = 1.0f - kDrag * dt;
    velocity->dx *= damping;
    velocity->dy *= damping;

    const float speed = std::hypot(velocity->dx, velocity->dy);
    if (speed > kMaxSpeed) {
        velocity->dx = velocity->dx / speed * kMaxSpeed;
        velocity->dy = velocity->dy / speed * kMaxSpeed;
    }

    // Keep the flame glued to the ship, and light it only under thrust.
    if (Transform* flameTransform = world.getComponent<Transform>(state.flame)) {
        *flameTransform = *transform;
    }
    if (Polygon* flame = world.getComponent<Polygon>(state.flame)) {
        flame->a = thrusting ? 255 : 0;
    }

    state.fireCooldown -= dt;
    if (input.isKeyDown(SDL_SCANCODE_SPACE) && state.fireCooldown <= 0.0f) {
        fireBullet(world, state);
    }
}

void breakRock(World& world, GameState& state, Entity rock) {
    Rock* data = world.getComponent<Rock>(rock);
    Transform* transform = world.getComponent<Transform>(rock);
    if (!data || !transform) return;

    state.score += kRockScore[data->size];

    // Large breaks into two mediums, medium into two smalls, small into
    // nothing. Note this ADDS entities while the caller is working through a
    // list of collisions — safe, because that list is a plain vector taken
    // before any of this ran, not a live view into a component pool.
    if (data->size > 0) {
        for (int i = 0; i < 2; ++i) {
            spawnRock(world, state, transform->x, transform->y, data->size - 1);
        }
    }

    world.destroyLater(rock);
}

void killShip(World& world, GameState& state) {
    world.destroyLater(state.ship);
    world.destroyLater(state.flame);
    state.ship = kInvalidEntity;
    state.flame = kInvalidEntity;

    --state.lives;
    if (state.lives <= 0) {
        state.gameOver = true;
    } else {
        state.respawnDelay = kRespawnDelay;
    }
}

void handleCollisions(World& world, GameState& state) {
    // Entities queued for destruction stay alive until the end of the frame,
    // so the same rock can turn up in several pairs. This remembers what has
    // already been dealt with, so one rock isn't scored or split twice.
    std::unordered_set<Entity> resolved;

    for (const CollisionPair& pair : CollisionSystem(world)) {
        // Which of the two is the bullet, and which the rock?
        Entity bullet = kInvalidEntity;
        Entity rock = kInvalidEntity;
        if (world.hasComponent<Bullet>(pair.a) && world.hasComponent<Rock>(pair.b)) {
            bullet = pair.a;
            rock = pair.b;
        } else if (world.hasComponent<Bullet>(pair.b) &&
                   world.hasComponent<Rock>(pair.a)) {
            bullet = pair.b;
            rock = pair.a;
        }

        if (bullet != kInvalidEntity) {
            if (resolved.count(bullet) || resolved.count(rock)) continue;
            resolved.insert(bullet);
            resolved.insert(rock);

            world.destroyLater(bullet);
            breakRock(world, state, rock);
            continue;
        }

        // Otherwise: did a rock hit the ship?
        const bool shipHit =
            (pair.a == state.ship && world.hasComponent<Rock>(pair.b)) ||
            (pair.b == state.ship && world.hasComponent<Rock>(pair.a));

        if (shipHit && state.ship != kInvalidEntity &&
            state.spawnProtection <= 0.0f) {
            killShip(world, state);
        }
    }
}

// --- Scenes ----------------------------------------------------------------

class GameOverScene : public Scene {
public:
    GameOverScene(int score, int wave) : score_(score), wave_(wave) {}

    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "GAME OVER", 230, 6,
                                            240, 100, 100));
        owned_.push_back(createCenteredText(
            world, "SCORE: " + std::to_string(score_), 320, 3, 235, 235, 235));
        owned_.push_back(createCenteredText(
            world, "WAVES CLEARED: " + std::to_string(wave_ - 1), 365, 2,
            170, 170, 195));
        owned_.push_back(createCenteredText(world, "R TO PLAY AGAIN", 430, 2,
                                            170, 170, 195));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 460, 2,
                                            170, 170, 195));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World& /*world*/, InputManager& input, float /*dt*/,
                SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_R)) scenes.pop();
    }

private:
    int score_;
    int wave_;
    std::vector<Entity> owned_;
};

class PauseScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "PAUSED", 280, 6,
                                            235, 235, 235));
        owned_.push_back(createCenteredText(world, "P TO RESUME", 370, 2,
                                            170, 170, 195));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World& /*world*/, InputManager& input, float /*dt*/,
                SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_P)) scenes.pop();
    }

private:
    std::vector<Entity> owned_;
};

class PlayScene : public Scene {
public:
    void onEnter(World& world) override {
        scoreText_ = createText(world, "SCORE: 0", 16, 14, 3, 200, 200, 215);

        // Lives are drawn as textured ship icons — the one place this game
        // uses the Sprite path instead of a Polygon. All of them are created
        // up front and switched on or off with alpha, rather than being
        // destroyed and rebuilt every time a life is lost.
        for (int i = 0; i < kStartLives; ++i) {
            Entity icon = world.createEntity();
            world.addComponent(
                icon, Transform{static_cast<float>(kWindowWidth - 44 - i * 34),
                                12.0f, 0.0f});
            Sprite sprite;
            sprite.width = 28;
            sprite.height = 28;
            sprite.texture = spriteSheet;
            sprite.srcX = kShipIconTile * 32;
            sprite.srcY = 0;
            sprite.srcW = 32;
            sprite.srcH = 32;
            sprite.layer = kFieldLayer;
            world.addComponent(icon, sprite);
            livesIcons_.push_back(icon);
        }

        spawnShip(world, state_);
        spawnWave(world, state_);
    }

    void onExit(World& world) override {
        // Everything this scene put on the field, whoever created it.
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Rock>(entity) ||
                world.hasComponent<Bullet>(entity) ||
                world.hasComponent<Ship>(entity)) {
                world.destroyLater(entity);
            }
        }
        world.destroyLater(state_.flame);
        world.destroyLater(scoreText_);
        for (Entity icon : livesIcons_) world.destroyLater(icon);
        livesIcons_.clear();
    }

    void onResume(World& world) override {
        // Coming back from the game-over overlay starts a fresh game;
        // coming back from pause must change nothing.
        if (!state_.gameOver) return;

        for (Entity entity : world.entities()) {
            if (world.hasComponent<Rock>(entity) ||
                world.hasComponent<Bullet>(entity)) {
                world.destroyLater(entity);
            }
        }

        state_.score = 0;
        state_.lives = kStartLives;
        state_.wave = 0;
        state_.gameOver = false;
        state_.respawnDelay = 0.0f;
        state_.fireCooldown = 0.0f;
        overlayShown_ = false;

        spawnShip(world, state_);
        spawnWave(world, state_);
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        if (state_.gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(state_.score,
                                                            state_.wave));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        // A cleared field means the next wave, one rock bigger than the last.
        if (countRocks(world) == 0) spawnWave(world, state_);

        state_.spawnProtection -= dt;
        controlShip(world, state_, input, dt);

        // MovementSystem has already moved everything by the time this
        // callback runs, so wrapping happens right after, on the new
        // positions, before anything is drawn.
        for (Entity entity : world.entities()) {
            if (!world.hasComponent<Velocity>(entity)) continue;
            if (Transform* transform = world.getComponent<Transform>(entity)) {
                wrapPosition(*transform);
            }
        }

        handleCollisions(world, state_);
        respawnIfNeeded(world, dt);
        blinkWhileProtected(world);
        refreshHud(world);
    }

private:
    void respawnIfNeeded(World& world, float dt) {
        if (state_.ship != kInvalidEntity || state_.gameOver) return;

        state_.respawnDelay -= dt;
        if (state_.respawnDelay > 0.0f) return;

        // The delay has passed, but the ship only returns once there is room
        // for it. Checked every frame until the rocks drift clear.
        if (!spawnAreaClear(world)) return;

        spawnShip(world, state_);
    }

    // A newly spawned ship flashes while it can't be hurt, so the rule is
    // visible rather than something the player has to infer.
    void blinkWhileProtected(World& world) {
        if (state_.ship == kInvalidEntity) return;

        Polygon* polygon = world.getComponent<Polygon>(state_.ship);
        if (!polygon) return;

        if (state_.spawnProtection <= 0.0f) {
            polygon->a = 255;
            return;
        }
        const float phase = std::fmod(state_.spawnProtection, 0.28f);
        polygon->a = (phase < 0.14f) ? 90 : 255;
    }

    void refreshHud(World& world) {
        if (state_.score != shownScore_) {
            if (Text* text = world.getComponent<Text>(scoreText_)) {
                text->value = "SCORE: " + std::to_string(state_.score);
            }
            shownScore_ = state_.score;
        }

        if (state_.lives != shownLives_) {
            for (std::size_t i = 0; i < livesIcons_.size(); ++i) {
                if (Sprite* sprite = world.getComponent<Sprite>(livesIcons_[i])) {
                    sprite->a = (static_cast<int>(i) < state_.lives) ? 255 : 0;
                }
            }
            shownLives_ = state_.lives;
        }
    }

    GameState state_;
    Entity scoreText_ = kInvalidEntity;
    std::vector<Entity> livesIcons_;
    int shownScore_ = -1;
    int shownLives_ = -1;
    bool overlayShown_ = false;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        // A slowly tumbling textured rock, which is the rotated-Sprite path:
        // AngularVelocity turns it and the renderer spins the image with
        // SDL_RenderCopyEx. The rocks in the game itself are Polygons, so
        // between the two screens both render paths are exercised under
        // rotation.
        decoration_ = world.createEntity();
        world.addComponent(decoration_,
                           Transform{static_cast<float>(kWindowWidth) / 2.0f - 80.0f,
                                     380.0f, 0.0f});
        world.addComponent(decoration_, AngularVelocity{0.35f});
        Sprite rock;
        rock.width = 160;
        rock.height = 160;
        rock.texture = spriteSheet;
        rock.srcX = 32;
        rock.srcY = 0;
        rock.srcW = 64;
        rock.srcH = 64;
        rock.a = 90;
        world.addComponent(decoration_, rock);

        owned_.push_back(createCenteredText(world, "ASTEROIDS", 120, 9,
                                            200, 230, 255));
        owned_.push_back(createCenteredText(world, "ARROWS TURN AND THRUST",
                                            250, 2, 200, 200, 215));
        owned_.push_back(createCenteredText(world, "SPACE FIRES - P PAUSES",
                                            280, 2, 170, 170, 195));
        owned_.push_back(createCenteredText(world, "SPACE TO START", 600, 3,
                                            235, 235, 235));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 650, 2,
                                            170, 170, 195));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
        world.destroyLater(decoration_);
    }

    void update(World& /*world*/, InputManager& input, float /*dt*/,
                SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            scenes.replace(std::make_unique<PlayScene>());
        } else if (input.wasKeyPressed(SDL_SCANCODE_Q)) {
            // Popping the last scene empties the stack, which the engine
            // treats as "quit".
            scenes.pop();
        }
    }

private:
    Entity decoration_ = kInvalidEntity;
    std::vector<Entity> owned_;
};

}  // namespace

int main(int, char**) {
    Engine gameEngine("Tiny Engine - Asteroids", kWindowWidth, kWindowHeight);
    World world;

    // Only the HUD icons and the title screen's rock need artwork; if it is
    // missing they fall back to plain rectangles and the game is unaffected.
    spriteSheet = gameEngine.textures().load("assets/asteroids.png");

    SceneStack scenes;
    scenes.push(std::make_unique<TitleScene>());

    gameEngine.run(world, scenes);
    return 0;
}
