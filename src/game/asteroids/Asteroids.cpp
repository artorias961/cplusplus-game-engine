// ---------------------------------------------------------------------------
// Asteroids.cpp — the rules.
//
// This is the engine's second game, and it exercises the half of it that Snake
// never touched: continuous motion under acceleration, rotation, circle
// colliders, and constant creation and destruction of entities.
//
// It uses both render paths deliberately. The ship, rocks, bullets and debris
// are vector Polygons — no assets, exact rotation, and every rock's outline is
// generated at spawn so no two are alike. The lives display and the title
// screen's tumbling rock are textured Sprites, so the texture path is covered
// too, including under rotation.
//
// None of this leaks into engine/. The engine has never heard of a rock; it
// sees Transforms, Velocities, Polygons and colliders.
// ---------------------------------------------------------------------------

#include "Asteroids.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/Font.h"
#include "engine/Systems.h"

using namespace engine;

namespace asteroids {
namespace {

// Set once at startup by main(); either may stay null, and everything below
// copes with that rather than requiring assets or a sound card to exist.
SDL_Texture* spriteSheet = nullptr;
AudioDevice* audioDevice = nullptr;

constexpr int kShipIconTile = 0;
constexpr float kDebrisLife = 0.7f;

// --- Sound -----------------------------------------------------------------
//
// Every sound in the game is arithmetic: a square wave for the gun, filtered
// randomness for an explosion. See engine/Audio.h.

void playFire() {
    if (audioDevice) audioDevice->play(Waveform::Square, 720.0f, 0.06f, 0.18f);
}

void playThrust() {
    if (audioDevice) audioDevice->play(Waveform::Noise, 0.0f, 0.09f, 0.05f);
}

// Bigger rocks break with a lower, longer boom, which is most of what makes
// the three sizes feel different.
void playRockBreak(int size) {
    if (!audioDevice) return;
    const float length = 0.16f + 0.06f * static_cast<float>(size);
    const float volume = 0.16f + 0.05f * static_cast<float>(size);
    audioDevice->play(Waveform::Noise, 0.0f, length, volume);
}

void playShipDeath() {
    if (!audioDevice) return;
    audioDevice->play(Waveform::Noise, 0.0f, 0.55f, 0.35f);
    audioDevice->play(Waveform::Sine, 90.0f, 0.45f, 0.25f);
}

// --- Maths -----------------------------------------------------------------

Vec2 heading(float radians) {
    return Vec2{std::cos(radians), std::sin(radians)};
}

float randomRange(std::mt19937& rng, float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(rng);
}

std::mt19937& rng() {
    // One generator for the whole game. Seeded once, so a session is varied
    // but a single run is reproducible if the seed is fixed for debugging.
    static std::mt19937 generator{std::random_device{}()};
    return generator;
}

// Space wraps: leave one edge and arrive at the opposite one.
void wrapPosition(Transform& transform) {
    const float width = static_cast<float>(kWindowWidth);
    const float height = static_cast<float>(kWindowHeight);

    if (transform.x < 0.0f) transform.x += width;
    else if (transform.x >= width) transform.x -= width;

    if (transform.y < 0.0f) transform.y += height;
    else if (transform.y >= height) transform.y -= height;
}

// --- Shapes ----------------------------------------------------------------

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
    polygon.layer = kFieldLayer;
    return polygon;
}

Polygon flamePolygon() {
    Polygon polygon;
    polygon.points = {Vec2{-7.0f, -5.0f}, Vec2{-19.0f, 0.0f}, Vec2{-7.0f, 5.0f}};
    polygon.closed = false;
    polygon.r = 255;
    polygon.g = 170;
    polygon.b = 60;
    polygon.a = 0;  // hidden until the player thrusts
    polygon.layer = kFieldLayer;
    return polygon;
}

// Every rock gets its own lumpy outline: points spaced evenly around a circle
// and then pushed in or out at random. It costs a few random numbers, and it
// is the kind of thing vector graphics make easy and a sprite sheet tedious.
Polygon rockPolygon(float radius) {
    Polygon polygon;
    const int pointCount = static_cast<int>(randomRange(rng(), 8.0f, 12.0f));

    for (int i = 0; i < pointCount; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) /
                            static_cast<float>(pointCount);
        const float distance = radius * randomRange(rng(), 0.72f, 1.14f);
        polygon.points.push_back(
            Vec2{std::cos(angle) * distance, std::sin(angle) * distance});
    }

    polygon.r = 190;
    polygon.g = 190;
    polygon.b = 205;
    polygon.layer = kFieldLayer;
    return polygon;
}

Polygon bulletPolygon() {
    Polygon polygon;
    polygon.points = {Vec2{-3.0f, 0.0f}, Vec2{3.0f, 0.0f}};
    polygon.closed = false;
    polygon.r = 255;
    polygon.g = 240;
    polygon.b = 180;
    polygon.layer = kFieldLayer;
    return polygon;
}

// --- Text ------------------------------------------------------------------

Entity createText(World& world, const std::string& value, int x, int y,
                  int scale, unsigned char r, unsigned char g, unsigned char b,
                  int layer) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{static_cast<float>(x),
                                         static_cast<float>(y), 0.0f});

    Text text{value, scale, r, g, b, 255};
    text.layer = layer;
    // Fixed to the screen, so it neither slides nor shakes with the camera.
    text.screenSpace = true;
    world.addComponent(entity, text);
    return entity;
}

Entity createCenteredText(World& world, const std::string& value, int y,
                          int scale, unsigned char r, unsigned char g,
                          unsigned char b, int layer) {
    const int x = (kWindowWidth - textWidth(value, scale)) / 2;
    return createText(world, value, x, y, scale, r, g, b, layer);
}

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
    panel.screenSpace = true;
    world.addComponent(entity, panel);
    return entity;
}

// --- Ghosts: drawing across the seam ---------------------------------------
//
// Wrapping by teleporting is correct but ugly: a rock's centre crosses the
// edge and the whole thing jumps to the far side, so it vanishes here and
// pops into existence there. The fix is to draw it twice while it straddles
// the boundary — once where it is, once a screen-width away.
//
// Each wrapping entity therefore gets one companion Ghost that copies its
// outline. The ghost is hidden (alpha 0) until its source comes within a
// margin of an edge, at which point it appears on the opposite side.
//
// One ghost covers one seam. An object exactly in a corner would need three
// to be perfect; that case is brief and rare enough to leave.
constexpr float kGhostMargin = 60.0f;

void attachGhost(World& world, Entity source, const Polygon& outline) {
    Entity ghost = world.createEntity();
    world.addComponent(ghost, Transform{});

    Polygon copy = outline;
    copy.a = 0;
    world.addComponent(ghost, copy);
    world.addComponent(ghost, Ghost{source});
}

void updateGhosts(World& world) {
    for (Entity entity : world.entities()) {
        Ghost* ghost = world.getComponent<Ghost>(entity);
        if (!ghost) continue;

        Polygon* ghostShape = world.getComponent<Polygon>(entity);
        Transform* ghostAt = world.getComponent<Transform>(entity);
        if (!ghostShape || !ghostAt) continue;

        Transform* sourceAt = world.getComponent<Transform>(ghost->source);
        if (!sourceAt) {
            // The source is gone; the ghost goes with it.
            world.destroyLater(entity);
            continue;
        }

        const float width = static_cast<float>(kWindowWidth);
        const float height = static_cast<float>(kWindowHeight);

        float offsetX = 0.0f;
        float offsetY = 0.0f;
        if (sourceAt->x < kGhostMargin) offsetX = width;
        else if (sourceAt->x > width - kGhostMargin) offsetX = -width;
        if (sourceAt->y < kGhostMargin) offsetY = height;
        else if (sourceAt->y > height - kGhostMargin) offsetY = -height;

        if (offsetX == 0.0f && offsetY == 0.0f) {
            ghostShape->a = 0;  // not near an edge: nothing to draw
            continue;
        }

        ghostAt->x = sourceAt->x + offsetX;
        ghostAt->y = sourceAt->y + offsetY;
        ghostAt->rotation = sourceAt->rotation;

        // Match whatever the source currently looks like, including a ship
        // blinking through its spawn protection.
        if (Polygon* sourceShape = world.getComponent<Polygon>(ghost->source)) {
            ghostShape->a = sourceShape->a;
        } else {
            ghostShape->a = 255;
        }
    }
}

// --- Debris ----------------------------------------------------------------
//
// Short line segments flung outward, spinning, that delete themselves. Every
// piece of this already existed — Polygon, Velocity, AngularVelocity,
// Lifetime — so an explosion needed no new engine feature at all, which is a
// fair sign the component set is pulling its weight.
void spawnDebris(World& world, float x, float y, int pieces, float speed,
                 unsigned char r, unsigned char g, unsigned char b) {
    for (int i = 0; i < pieces; ++i) {
        const float angle = randomRange(rng(), 0.0f, kTwoPi);
        const float length = randomRange(rng(), 4.0f, 11.0f);

        Entity piece = world.createEntity();
        world.addComponent(piece, Transform{x, y, angle});

        const float pieceSpeed = speed * randomRange(rng(), 0.45f, 1.2f);
        world.addComponent(piece, Velocity{std::cos(angle) * pieceSpeed,
                                           std::sin(angle) * pieceSpeed});
        world.addComponent(piece, AngularVelocity{randomRange(rng(), -6.0f, 6.0f)});

        Polygon shard;
        shard.points = {Vec2{-length * 0.5f, 0.0f}, Vec2{length * 0.5f, 0.0f}};
        shard.closed = false;
        shard.r = r;
        shard.g = g;
        shard.b = b;
        shard.layer = kFieldLayer;
        world.addComponent(piece, shard);

        world.addComponent(piece, Lifetime{kDebrisLife *
                                           randomRange(rng(), 0.6f, 1.3f)});
        world.addComponent(piece, Debris{});
    }
}

// --- Spawning --------------------------------------------------------------

void spawnShip(World& world, Session& session) {
    Entity ship = world.createEntity();
    // -pi/2 is "up": rotation 0 points right and y grows downward.
    world.addComponent(ship, Transform{static_cast<float>(kWindowWidth) / 2.0f,
                                       static_cast<float>(kWindowHeight) / 2.0f,
                                       -kPi / 2.0f});
    world.addComponent(ship, Velocity{});
    const Polygon outline = shipPolygon();
    world.addComponent(ship, outline);
    world.addComponent(ship, CircleCollider{kShipRadius});
    world.addComponent(ship, PlayerControlled{});
    world.addComponent(ship, Wrapping{});
    world.addComponent(ship, Ship{});
    attachGhost(world, ship, outline);

    // The flame is its own entity that copies the ship's Transform each frame.
    Entity flame = world.createEntity();
    world.addComponent(flame, Transform{});
    world.addComponent(flame, flamePolygon());

    session.spawnProtection = kSpawnProtection;
}

Entity spawnRock(World& world, float x, float y, int size) {
    const float radius = kRockRadius[size];

    Entity rock = world.createEntity();
    world.addComponent(rock, Transform{x, y, randomRange(rng(), 0.0f, kTwoPi)});

    const float angle = randomRange(rng(), 0.0f, kTwoPi);
    const float speed = randomRange(rng(), kRockSpeedMin[size],
                                    kRockSpeedMax[size]);
    world.addComponent(rock, Velocity{std::cos(angle) * speed,
                                      std::sin(angle) * speed});
    world.addComponent(rock, AngularVelocity{randomRange(rng(), -1.5f, 1.5f)});

    const Polygon outline = rockPolygon(radius);
    world.addComponent(rock, outline);

    // A little smaller than the drawn outline, because the outline's points
    // stick out past the average radius and a hitbox that punishes near
    // misses feels unfair.
    world.addComponent(rock, CircleCollider{radius * 0.85f});
    world.addComponent(rock, Wrapping{});
    world.addComponent(rock, Rock{size});
    attachGhost(world, rock, outline);

    return rock;
}

void spawnWave(World& world, Session& session) {
    ++session.wave;
    const int count = kStartRocks + session.wave - 1;

    const float centerX = static_cast<float>(kWindowWidth) / 2.0f;
    const float centerY = static_cast<float>(kWindowHeight) / 2.0f;

    for (int i = 0; i < count; ++i) {
        float x = 0.0f;
        float y = 0.0f;
        do {
            x = randomRange(rng(), 0.0f, static_cast<float>(kWindowWidth));
            y = randomRange(rng(), 0.0f, static_cast<float>(kWindowHeight));
        } while (std::hypot(x - centerX, y - centerY) < kSafeSpawnDistance);

        spawnRock(world, x, y, kLargeRock);
    }
}

Entity findFlame(World& world) {
    // The flame is the one entity with a Polygon but no collider and no tag.
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Polygon>(entity)) continue;
        if (world.hasComponent<Rock>(entity)) continue;
        if (world.hasComponent<Bullet>(entity)) continue;
        if (world.hasComponent<Ship>(entity)) continue;
        if (world.hasComponent<Debris>(entity)) continue;
        if (world.hasComponent<Ghost>(entity)) continue;
        return entity;
    }
    return kInvalidEntity;
}

}  // namespace

// --- Queries ---------------------------------------------------------------

Session* findSession(World& world) {
    for (Entity entity : world.entities()) {
        if (Session* session = world.getComponent<Session>(entity)) {
            return session;
        }
    }
    return nullptr;
}

Entity findShip(World& world) {
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Ship>(entity)) return entity;
    }
    return kInvalidEntity;
}

int countRocks(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Rock>(entity)) ++count;
    }
    return count;
}

int countBullets(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Bullet>(entity)) ++count;
    }
    return count;
}

void setSpriteSheet(SDL_Texture* sheet) { spriteSheet = sheet; }
void setAudioDevice(AudioDevice* audio) { audioDevice = audio; }

namespace {

// --- Scenes ----------------------------------------------------------------

class GameOverScene : public Scene {
public:
    GameOverScene(int score, int wave) : score_(score), wave_(wave) {}

    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "GAME OVER", 230, 6,
                                            240, 100, 100, kOverlayTextLayer));
        owned_.push_back(createCenteredText(
            world, "SCORE: " + std::to_string(score_), 320, 3, 235, 235, 235,
            kOverlayTextLayer));
        owned_.push_back(createCenteredText(
            world, "WAVES CLEARED: " + std::to_string(wave_ - 1), 365, 2,
            170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "R TO PLAY AGAIN", 430, 2,
                                            170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 460, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_R)) scenes.pop();
    }

    // An overlay over a finished game: the world holds still behind it.
    bool simulatesWorld() const override { return false; }

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
                                            235, 235, 235, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "P TO RESUME", 370, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_P)) scenes.pop();
    }

    // The whole point of a pause: stop the engine moving things too.
    bool simulatesWorld() const override { return false; }

private:
    std::vector<Entity> owned_;
};

class PlayScene : public Scene {
public:
    void onEnter(World& world) override {
        sessionEntity_ = world.createEntity();
        world.addComponent(sessionEntity_, Session{});

        // The camera exists so the view can be shaken. Nothing else moves it,
        // so the field sits exactly where it always did.
        cameraEntity_ = world.createEntity();
        world.addComponent(cameraEntity_, Camera{});

        scoreText_ = createText(world, "SCORE: 0", 16, 14, 3, 200, 200, 215,
                                kHudLayer);

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
            sprite.layer = kHudLayer;
            sprite.screenSpace = true;
            world.addComponent(icon, sprite);
            livesIcons_.push_back(icon);
        }

        Session* session = world.getComponent<Session>(sessionEntity_);
        spawnShip(world, *session);
        spawnWave(world, *session);
    }

    void onExit(World& world) override {
        clearField(world);
        world.destroyLater(scoreText_);
        world.destroyLater(cameraEntity_);
        world.destroyLater(sessionEntity_);
        for (Entity icon : livesIcons_) world.destroyLater(icon);
        livesIcons_.clear();
    }

    void onResume(World& world) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session || !session->gameOver) return;  // returning from pause

        clearField(world);
        *session = Session{};
        shownScore_ = -1;
        shownLives_ = -1;
        overlayShown_ = false;

        spawnShip(world, *session);
        spawnWave(world, *session);
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session) return;

        if (session->gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(session->score,
                                                            session->wave));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        if (countRocks(world) == 0) spawnWave(world, *session);

        session->spawnProtection -= dt;
        controlShip(world, *session, input, dt);

        // MovementSystem has already moved everything, so wrapping happens
        // here on the new positions, before anything is drawn.
        for (Entity entity : world.entities()) {
            if (!world.hasComponent<Wrapping>(entity)) continue;
            if (Transform* transform = world.getComponent<Transform>(entity)) {
                wrapPosition(*transform);
            }
        }

        handleCollisions(world, *session);
        respawnIfNeeded(world, *session, dt);
        blinkWhileProtected(world, *session);
        updateGhosts(world);
        updateShake(world, *session, dt);
        refreshHud(world, *session);
    }

private:
    void clearField(World& world) {
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Rock>(entity) ||
                world.hasComponent<Bullet>(entity) ||
                world.hasComponent<Ship>(entity) ||
                world.hasComponent<Debris>(entity) ||
                world.hasComponent<Ghost>(entity)) {
                world.destroyLater(entity);
            }
        }
        if (Entity flame = findFlame(world); flame != kInvalidEntity) {
            world.destroyLater(flame);
        }
    }

    void controlShip(World& world, Session& session, InputManager& input,
                     float dt) {
        const Entity ship = findShip(world);
        if (ship == kInvalidEntity) return;

        Transform* transform = world.getComponent<Transform>(ship);
        Velocity* velocity = world.getComponent<Velocity>(ship);
        if (!transform || !velocity) return;

        if (input.isKeyDown(SDL_SCANCODE_LEFT)) transform->rotation -= kTurnSpeed * dt;
        if (input.isKeyDown(SDL_SCANCODE_RIGHT)) transform->rotation += kTurnSpeed * dt;

        const bool thrusting = input.isKeyDown(SDL_SCANCODE_UP);
        if (thrusting) {
            const Vec2 direction = heading(transform->rotation);
            velocity->dx += direction.x * kThrust * dt;
            velocity->dy += direction.y * kThrust * dt;

            // Short bursts of noise, restarted as the last one fades, add up
            // to a rumble without needing looping sound support.
            thrustSound_ -= dt;
            if (thrustSound_ <= 0.0f) {
                playThrust();
                thrustSound_ = 0.07f;
            }
        }

        // Drag, so the ship coasts to a stop rather than drifting forever.
        // Multiplying keeps the slowdown proportional to the current speed.
        const float damping = 1.0f - kDrag * dt;
        velocity->dx *= damping;
        velocity->dy *= damping;

        const float speed = std::hypot(velocity->dx, velocity->dy);
        if (speed > kMaxSpeed) {
            velocity->dx = velocity->dx / speed * kMaxSpeed;
            velocity->dy = velocity->dy / speed * kMaxSpeed;
        }

        const Entity flame = findFlame(world);
        if (flame != kInvalidEntity) {
            if (Transform* flameAt = world.getComponent<Transform>(flame)) {
                *flameAt = *transform;
            }
            if (Polygon* flameShape = world.getComponent<Polygon>(flame)) {
                flameShape->a = thrusting ? 255 : 0;
            }
        }

        session.spawnProtection = session.spawnProtection;  // (unchanged here)
        fireCooldown_ -= dt;
        if (input.isKeyDown(SDL_SCANCODE_SPACE) && fireCooldown_ <= 0.0f) {
            fireBullet(world, ship);
        }
    }

    void fireBullet(World& world, Entity ship) {
        Transform* shipAt = world.getComponent<Transform>(ship);
        Velocity* shipVelocity = world.getComponent<Velocity>(ship);
        if (!shipAt || !shipVelocity) return;

        const Vec2 direction = heading(shipAt->rotation);

        Entity bullet = world.createEntity();
        world.addComponent(bullet, Transform{shipAt->x + direction.x * 16.0f,
                                             shipAt->y + direction.y * 16.0f,
                                             shipAt->rotation});
        // The ship's own velocity is added in, so shots fired while flying
        // fast keep up instead of being left behind.
        world.addComponent(bullet,
                           Velocity{shipVelocity->dx + direction.x * kBulletSpeed,
                                    shipVelocity->dy + direction.y * kBulletSpeed});
        world.addComponent(bullet, bulletPolygon());
        world.addComponent(bullet, CircleCollider{kBulletRadius});
        world.addComponent(bullet, Wrapping{});
        // No bookkeeping list of live bullets: LifetimeSystem deletes it.
        world.addComponent(bullet, Lifetime{kBulletLife});
        world.addComponent(bullet, Bullet{});

        fireCooldown_ = kFireCooldown;
        playFire();
    }

    void breakRock(World& world, Session& session, Entity rock) {
        Rock* data = world.getComponent<Rock>(rock);
        Transform* transform = world.getComponent<Transform>(rock);
        if (!data || !transform) return;

        session.score += kRockScore[data->size];
        playRockBreak(data->size);
        spawnDebris(world, transform->x, transform->y, 5 + data->size * 2,
                    90.0f + 30.0f * static_cast<float>(data->size),
                    190, 190, 205);

        // A small kick to the view, larger for larger rocks.
        addShake(session, 0.12f + 0.04f * static_cast<float>(data->size),
                 2.0f + 1.5f * static_cast<float>(data->size));

        // Large breaks into two mediums, medium into two smalls, small into
        // nothing. Note this ADDS entities while the caller is working through
        // a list of collisions — safe, because that list is a plain vector
        // taken before any of this ran, not a live view into a pool.
        if (data->size > 0) {
            for (int i = 0; i < 2; ++i) {
                spawnRock(world, transform->x, transform->y, data->size - 1);
            }
        }

        world.destroyLater(rock);
    }

    void killShip(World& world, Session& session, Entity ship) {
        if (Transform* at = world.getComponent<Transform>(ship)) {
            spawnDebris(world, at->x, at->y, 10, 150.0f, 200, 230, 255);
        }
        playShipDeath();
        addShake(session, 0.45f, 9.0f);

        world.destroyLater(ship);
        if (Entity flame = findFlame(world); flame != kInvalidEntity) {
            world.destroyLater(flame);
        }

        --session.lives;
        if (session.lives <= 0) {
            session.gameOver = true;
        } else {
            session.respawnDelay = kRespawnDelay;
        }
    }

    void handleCollisions(World& world, Session& session) {
        // Entities queued for destruction stay alive until the end of the
        // frame, so the same rock can turn up in several pairs. This
        // remembers what has been dealt with, so nothing is scored twice.
        std::unordered_set<Entity> resolved;
        const Entity ship = findShip(world);

        for (const CollisionPair& pair : CollisionSystem(world)) {
            Entity bullet = kInvalidEntity;
            Entity rock = kInvalidEntity;
            if (world.hasComponent<Bullet>(pair.a) &&
                world.hasComponent<Rock>(pair.b)) {
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
                breakRock(world, session, rock);
                continue;
            }

            const bool shipHit =
                (pair.a == ship && world.hasComponent<Rock>(pair.b)) ||
                (pair.b == ship && world.hasComponent<Rock>(pair.a));

            if (shipHit && ship != kInvalidEntity && !resolved.count(ship) &&
                session.spawnProtection <= 0.0f) {
                resolved.insert(ship);
                killShip(world, session, ship);
            }
        }
    }

    // A new ship only appears once there is room for it. Without this it
    // returns to the middle regardless of what is sitting there, spawn
    // protection hides the problem for a couple of seconds, and then it dies
    // the instant that wears off — repeatedly, without the player touching a
    // control. Rocks always move, so waiting for a gap always terminates.
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

    void respawnIfNeeded(World& world, Session& session, float dt) {
        if (findShip(world) != kInvalidEntity || session.gameOver) return;

        session.respawnDelay -= dt;
        if (session.respawnDelay > 0.0f) return;
        if (!spawnAreaClear(world)) return;

        spawnShip(world, session);
    }

    // A newly spawned ship flashes while it can't be hurt, so the rule is
    // visible rather than something the player has to infer.
    void blinkWhileProtected(World& world, const Session& session) {
        const Entity ship = findShip(world);
        if (ship == kInvalidEntity) return;

        Polygon* polygon = world.getComponent<Polygon>(ship);
        if (!polygon) return;

        if (session.spawnProtection <= 0.0f) {
            polygon->a = 255;
            return;
        }
        const float phase = std::fmod(session.spawnProtection, 0.28f);
        polygon->a = (phase < 0.14f) ? 90 : 255;
    }

    static void addShake(Session& session, float seconds, float magnitude) {
        // Take the stronger of the two rather than adding, so a burst of
        // small hits can't stack into an unreadable screen.
        session.shakeSeconds = std::max(session.shakeSeconds, seconds);
        session.shakeMagnitude = std::max(session.shakeMagnitude, magnitude);
    }

    // Screen shake is just the camera being nudged somewhere random each
    // frame, by an amount that fades out. It costs nothing and does more for
    // the feel of an impact than any amount of extra artwork.
    void updateShake(World& world, Session& session, float dt) {
        Camera* camera = world.getComponent<Camera>(cameraEntity_);
        if (!camera) return;

        if (session.shakeSeconds <= 0.0f) {
            camera->x = 0.0f;
            camera->y = 0.0f;
            session.shakeMagnitude = 0.0f;
            return;
        }

        session.shakeSeconds -= dt;
        const float strength = session.shakeMagnitude *
                               std::max(0.0f, session.shakeSeconds);
        camera->x = randomRange(rng(), -strength, strength);
        camera->y = randomRange(rng(), -strength, strength);
    }

    void refreshHud(World& world, const Session& session) {
        if (session.score != shownScore_) {
            if (Text* text = world.getComponent<Text>(scoreText_)) {
                text->value = "SCORE: " + std::to_string(session.score);
            }
            shownScore_ = session.score;
        }
        if (session.lives != shownLives_) {
            for (std::size_t i = 0; i < livesIcons_.size(); ++i) {
                if (Sprite* sprite = world.getComponent<Sprite>(livesIcons_[i])) {
                    sprite->a = (static_cast<int>(i) < session.lives) ? 255 : 0;
                }
            }
            shownLives_ = session.lives;
        }
    }

    Entity sessionEntity_ = kInvalidEntity;
    Entity cameraEntity_ = kInvalidEntity;
    Entity scoreText_ = kInvalidEntity;
    std::vector<Entity> livesIcons_;
    float fireCooldown_ = 0.0f;
    float thrustSound_ = 0.0f;
    int shownScore_ = -1;
    int shownLives_ = -1;
    bool overlayShown_ = false;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        // A slowly tumbling textured rock: the rotated-Sprite path, where the
        // game itself uses Polygons. Between the two screens both render paths
        // are covered under rotation.
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
        rock.layer = kFieldLayer;
        world.addComponent(decoration_, rock);

        owned_.push_back(createCenteredText(world, "ASTEROIDS", 120, 9,
                                            200, 230, 255, kHudLayer));
        owned_.push_back(createCenteredText(world, "ARROWS TURN AND THRUST",
                                            250, 2, 200, 200, 215, kHudLayer));
        owned_.push_back(createCenteredText(world, "SPACE FIRES - P PAUSES",
                                            280, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(world, "SPACE TO START", 600, 3,
                                            235, 235, 235, kHudLayer));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 650, 2,
                                            170, 170, 195, kHudLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
        world.destroyLater(decoration_);
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            scenes.replace(makePlayScene());
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

std::unique_ptr<Scene> makeTitleScene() {
    return std::make_unique<TitleScene>();
}

std::unique_ptr<Scene> makePlayScene() {
    return std::make_unique<PlayScene>();
}

}  // namespace asteroids
