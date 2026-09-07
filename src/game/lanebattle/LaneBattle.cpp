// ---------------------------------------------------------------------------
// LaneBattle.cpp — the rules.
//
// The first slice of the engine's fourth game, and the first one that adds no
// engine features at all. That is the point: prove the loop is a game before
// paying for a camera, a mouse, or artwork.
//
// Two things worth noticing while reading:
//
//   - Nothing here uses CollisionSystem. A lane battler asks "is the nearest
//     enemy ahead of me within reach?", which is a comparison of x positions,
//     not an overlap of shapes. Reaching for the collision system because it
//     exists would be slower and less clear. Later slices may change that,
//     and the benchmark will decide.
//   - Units walk because they have a Velocity and the engine moves anything
//     with one. Stopping to fight is done by setting that velocity to zero;
//     there is no separate "walking" state to keep in sync.
// ---------------------------------------------------------------------------

#include "LaneBattle.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/Font.h"
#include "engine/Systems.h"

using namespace engine;

namespace lanebattle {
namespace {

AudioDevice* audioDevice = nullptr;

// Where each side's castle stands, and where its units appear just clear of
// it. All measured against the WORLD, not the window — the window is only how
// much of the world you happen to be looking at.
constexpr float kLeftCastleX = kCastleMargin;
constexpr float kRightCastleX = kWorldWidth - kCastleMargin - kCastleWidth;

constexpr float kLeftSpawnX = kLeftCastleX + kCastleWidth + 4.0f;
constexpr float kRightSpawnX = kRightCastleX - kUnitWidth - 4.0f;

// --- Sound -----------------------------------------------------------------

void playSpawn() {
    if (audioDevice) audioDevice->play(Waveform::Square, 300.0f, 0.05f, 0.12f);
}

void playHit() {
    if (audioDevice) audioDevice->play(Waveform::Noise, 0.0f, 0.04f, 0.07f);
}

void playDeath() {
    if (audioDevice) audioDevice->play(Waveform::Noise, 0.0f, 0.18f, 0.16f);
}

void playCastleHit() {
    if (audioDevice) audioDevice->play(Waveform::Sine, 120.0f, 0.12f, 0.18f);
}

// --- Building blocks -------------------------------------------------------

Entity createRect(World& world, float x, float y, float width, float height,
                  unsigned char r, unsigned char g, unsigned char b,
                  int layer) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{x, y, 0.0f});

    Sprite sprite;
    sprite.width = static_cast<int>(width);
    sprite.height = static_cast<int>(height);
    sprite.r = r;
    sprite.g = g;
    sprite.b = b;
    sprite.layer = layer;
    world.addComponent(entity, sprite);
    return entity;
}

Entity createText(World& world, const std::string& value, int x, int y,
                  int scale, unsigned char r, unsigned char g, unsigned char b,
                  int layer) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{static_cast<float>(x),
                                         static_cast<float>(y), 0.0f});

    Text text{value, scale, r, g, b, 255};
    text.layer = layer;
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
    panel.a = 180;
    panel.layer = kOverlayLayer;
    panel.screenSpace = true;
    world.addComponent(entity, panel);
    return entity;
}

// A unit's forward direction: +1 for the left side, -1 for the right.
float facing(bool leftSide) { return leftSide ? 1.0f : -1.0f; }

// --- Combat ----------------------------------------------------------------

// The nearest enemy ahead of `attacker` and within reach, or kInvalidEntity.
//
// "Ahead" and "within reach" are both decided on x alone, because that is what
// a lane is. Castles count as enemies too, which is what lets the same code
// handle a unit reaching the end of the lane.
//
// This is a linear scan over every unit, run once per unit per frame — O(n²)
// overall. With a dozen units that is free. When it stops being free,
// engine_bench will say so and the answer will be a spatial grid; guessing at
// that now would be optimising a number nobody has measured.
Entity findTargetAhead(World& world, Entity attacker, bool leftSide,
                       float attackerX) {
    const float direction = facing(leftSide);
    const float attackerCenter = attackerX + kUnitWidth / 2.0f;

    Entity best = kInvalidEntity;
    float bestGap = kUnitRange;

    for (Entity other : world.entities()) {
        if (other == attacker) continue;

        Team* team = world.getComponent<Team>(other);
        if (!team || team->leftSide == leftSide) continue;  // friend, or teamless
        if (!world.hasComponent<Unit>(other) &&
            !world.hasComponent<Castle>(other)) {
            continue;
        }

        Transform* transform = world.getComponent<Transform>(other);
        Sprite* sprite = world.getComponent<Sprite>(other);
        if (!transform || !sprite) continue;

        // Measured between the facing EDGES, not between the Transforms.
        // Transforms sit at the left edge, so comparing them directly makes
        // reach depend on which side you approach from — a unit could be in
        // range of a 70-pixel castle from one side and have to walk through
        // it from the other. Centres decide what counts as "ahead"; half
        // widths turn that into a gap between surfaces.
        const float targetWidth = static_cast<float>(sprite->width);
        const float targetCenter = transform->x + targetWidth / 2.0f;

        const float centreAhead = (targetCenter - attackerCenter) * direction;
        if (centreAhead < 0.0f) continue;  // behind us

        const float gap = centreAhead - (kUnitWidth + targetWidth) / 2.0f;
        if (gap > bestGap) continue;

        bestGap = gap;
        best = other;
    }
    return best;
}

void spawnShards(World& world, float x, float y, unsigned char r,
                 unsigned char g, unsigned char b) {
    // Six pieces thrown out of a dying unit. Polygon + Velocity + Lifetime,
    // all of which already existed — a death effect needed no engine work.
    for (int i = 0; i < 6; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / 6.0f;

        Entity shard = world.createEntity();
        world.addComponent(shard, Transform{x, y, angle});
        world.addComponent(shard, Velocity{std::cos(angle) * 70.0f,
                                           std::sin(angle) * 70.0f - 30.0f});
        world.addComponent(shard, AngularVelocity{4.0f});

        Polygon piece;
        piece.points = {Vec2{-4.0f, 0.0f}, Vec2{4.0f, 0.0f}};
        piece.closed = false;
        piece.r = r;
        piece.g = g;
        piece.b = b;
        piece.layer = kFieldLayer;
        world.addComponent(shard, piece);

        world.addComponent(shard, Lifetime{0.45f});
        world.addComponent(shard, Shard{});
    }
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

Entity findCastle(World& world, bool leftSide) {
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Castle>(entity)) continue;
        Team* team = world.getComponent<Team>(entity);
        if (team && team->leftSide == leftSide) return entity;
    }
    return kInvalidEntity;
}

int countUnits(World& world, bool leftSide) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Unit>(entity)) continue;
        Team* team = world.getComponent<Team>(entity);
        if (team && team->leftSide == leftSide) ++count;
    }
    return count;
}

Camera* findCamera(World& world) {
    for (auto& entry : world.view<Camera>()) return &entry.second;
    return nullptr;
}

float frontLineX(World& world, bool leftSide) {
    const float direction = facing(leftSide);

    bool found = false;
    float front = 0.0f;
    for (Entity entity : world.entities()) {
        if (!world.hasComponent<Unit>(entity)) continue;
        Team* team = world.getComponent<Team>(entity);
        Transform* transform = world.getComponent<Transform>(entity);
        if (!team || team->leftSide != leftSide || !transform) continue;

        // "Frontmost" is furthest along your own direction of travel, which
        // is the largest x for the left side and the smallest for the right.
        // Multiplying by the direction lets one comparison serve both.
        if (!found || transform->x * direction > front * direction) {
            front = transform->x;
            found = true;
        }
    }
    if (found) return front;

    // Nothing on the field: your advance has reached your own front door.
    const Entity castle = findCastle(world, leftSide);
    if (castle != kInvalidEntity) {
        if (Transform* transform = world.getComponent<Transform>(castle)) {
            return transform->x + kCastleWidth / 2.0f;
        }
    }
    // Unreachable while a battle is running — a side always has a castle, and
    // the scene stops calling this the moment one falls. Kept because the
    // function has to return something when findCastle finds nothing, and
    // noted because no test can reach it: breaking this line on purpose left
    // every test passing, which is a fact about the line, not the tests.
    return leftSide ? kLeftCastleX : kRightCastleX;
}

Entity spawnUnit(World& world, bool leftSide) {
    const float x = leftSide ? kLeftSpawnX : kRightSpawnX;

    Entity unit = world.createEntity();
    world.addComponent(unit, Transform{x, kGroundY - kUnitHeight, 0.0f});
    world.addComponent(unit, Velocity{kUnitSpeed * facing(leftSide), 0.0f});

    Sprite sprite;
    sprite.width = static_cast<int>(kUnitWidth);
    sprite.height = static_cast<int>(kUnitHeight);
    if (leftSide) {
        sprite.r = 110; sprite.g = 190; sprite.b = 240;
    } else {
        sprite.r = 235; sprite.g = 130; sprite.b = 110;
    }
    sprite.layer = kFieldLayer;
    world.addComponent(unit, sprite);

    world.addComponent(unit, Team{leftSide});
    world.addComponent(unit, Unit{});

    playSpawn();
    return unit;
}

void setAudioDevice(AudioDevice* audio) { audioDevice = audio; }

namespace {

// --- Scenes ----------------------------------------------------------------

class GameOverScene : public Scene {
public:
    explicit GameOverScene(bool playerWon) : playerWon_(playerWon) {}

    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(
            world, playerWon_ ? "VICTORY" : "DEFEAT", 170, 6,
            playerWon_ ? 140 : 240, playerWon_ ? 230 : 110,
            playerWon_ ? 150 : 110, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "R TO FIGHT AGAIN", 280, 2,
                                            170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 310, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_R)) scenes.pop();
    }

    bool simulatesWorld() const override { return false; }

private:
    bool playerWon_;
    std::vector<Entity> owned_;
};

class PauseScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "PAUSED", 200, 6,
                                            235, 235, 235, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "P TO RESUME", 280, 2,
                                            170, 170, 195, kOverlayTextLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_P)) scenes.pop();
    }

    bool simulatesWorld() const override { return false; }

private:
    std::vector<Entity> owned_;
};

class PlayScene : public Scene {
public:
    void onEnter(World& world) override {
        sessionEntity_ = world.createEntity();
        world.addComponent(sessionEntity_, Session{});

        // One camera, on its own entity. The renderer picks up the first one
        // it finds; before this game the only thing that ever moved it was
        // Asteroids' screen shake, a few pixels at a time.
        cameraEntity_ = world.createEntity();
        world.addComponent(cameraEntity_, Camera{});

        buildField(world);

        goldText_ = createText(world, "GOLD 150", 16, 14, 3, 235, 220, 150,
                               kHudLayer);
        leftHealthText_ = createText(world, "", 16, 48, 2, 140, 200, 240,
                                     kHudLayer);
        rightHealthText_ = createText(world, "", kWindowWidth - 220, 48, 2,
                                      240, 150, 130, kHudLayer);
        hud_.push_back(goldText_);
        hud_.push_back(leftHealthText_);
        hud_.push_back(rightHealthText_);
        // The old hint read "A SPAWNS", which taught the one strategy that
        // cannot win: spending the instant you can afford to is exactly what
        // the opponent does, and mirroring it deadlocks the field forever.
        // Banking for a wave is the actual game, so the hint says so.
        hud_.push_back(createText(world,
                                  "A SENDS - SAVE FOR A WAVE - ARROWS LOOK - P PAUSES",
                                  16, kWindowHeight - 30, 2, 150, 150, 175,
                                  kHudLayer));

        buildMinimap(world);

        // Start looking at your own castle rather than at the origin, so the
        // first frame is not a lurch.
        snapCameraToTarget(world);
    }

    void onExit(World& world) override {
        clearField(world);
        world.destroyLater(sessionEntity_);
        world.destroyLater(cameraEntity_);
        for (Entity entity : hud_) world.destroyLater(entity);
        hud_.clear();
    }

    void onResume(World& world) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session || !session->gameOver) return;  // returning from pause

        clearField(world);
        *session = Session{};
        buildField(world);
        snapCameraToTarget(world);
        overlayShown_ = false;
        shownGold_ = -1;
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session) return;

        if (session->gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(session->playerWon));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        earnGold(*session, dt);
        handleSpawning(world, *session, input, dt);
        fight(world, *session, dt);
        removeTheDead(world, *session);

        // After the dead are gone, so the camera never chases a corpse for a
        // frame, and after the fight, so the minimap shows this frame's front
        // line rather than last frame's.
        updateCamera(world, *session, input, dt);
        refreshHud(world, *session);
        refreshMinimap(world);
    }

private:
    void buildField(World& world) {
        // The ground, drawn as one wide bar so the units have something to
        // stand on rather than floating in the dark. It spans the WORLD now,
        // not the window, or it would run out halfway through the first push.
        field_.push_back(createRect(world, 0.0f, kGroundY, kWorldWidth,
                                    static_cast<float>(kWindowHeight) - kGroundY,
                                    38, 42, 52, kFieldLayer));

        buildDistanceMarkers(world);

        Entity left = createRect(world, kLeftCastleX, kGroundY - kCastleHeight,
                                 kCastleWidth, kCastleHeight, 70, 120, 165,
                                 kFieldLayer);
        world.addComponent(left, Team{true});
        world.addComponent(left, Castle{});
        field_.push_back(left);

        Entity right = createRect(world, kRightCastleX, kGroundY - kCastleHeight,
                                  kCastleWidth, kCastleHeight, 170, 90, 80,
                                  kFieldLayer);
        world.addComponent(right, Team{false});
        world.addComponent(right, Castle{});
        field_.push_back(right);
    }

    // Posts along the field at a fixed spacing.
    //
    // These are not decoration. A uniform ground bar under a moving camera
    // looks exactly like a stationary ground bar under a stationary camera —
    // with nothing at a fixed world position to slide past, a correct camera
    // and a broken one are indistinguishable. Something has to mark distance
    // for the scrolling to read as scrolling at all.
    void buildDistanceMarkers(World& world) {
        for (float x = 240.0f; x < kWorldWidth - 120.0f; x += 240.0f) {
            field_.push_back(createRect(world, x, kGroundY - 46.0f, 3.0f, 46.0f,
                                        52, 58, 70, kFieldLayer));
            field_.push_back(createRect(world, x - 6.0f, kGroundY - 52.0f, 15.0f,
                                        6.0f, 62, 70, 84, kFieldLayer));
        }
    }

    void clearField(World& world) {
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Unit>(entity) ||
                world.hasComponent<Shard>(entity)) {
                world.destroyLater(entity);
            }
        }
        for (Entity entity : field_) world.destroyLater(entity);
        field_.clear();
    }

    static void earnGold(Session& session, float dt) {
        session.gold += kGoldPerSecond * dt;
        session.enemyGold += kGoldPerSecond * kEnemyIncomeMultiplier * dt;
    }

    void handleSpawning(World& world, Session& session, InputManager& input,
                        float dt) {
        session.spawnCooldown -= dt;
        if (input.isKeyDown(SDL_SCANCODE_A) && session.spawnCooldown <= 0.0f &&
            session.gold >= kUnitCost) {
            session.gold -= kUnitCost;
            session.spawnCooldown = kSpawnCooldown;
            spawnUnit(world, true);
        }

        // The opponent, playing by the same rules from the same purse. Its
        // whole strategy is "spend as soon as you can", which is a decent
        // baseline and leaves difficulty as one multiplier on its income.
        session.enemySpawnTimer -= dt;
        if (session.enemySpawnTimer <= 0.0f && session.enemyGold >= kUnitCost) {
            session.enemyGold -= kUnitCost;
            session.enemySpawnTimer = kSpawnCooldown;
            spawnUnit(world, false);
        }
    }

    // Each unit either walks or fights, never both.
    void fight(World& world, Session& session, float dt) {
        // Gathered first, because attacking creates shards and killing blows
        // queue deletions — neither of which should happen while iterating
        // the live entity list.
        std::vector<Entity> units;
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Unit>(entity)) units.push_back(entity);
        }

        for (Entity attacker : units) {
            Unit* unit = world.getComponent<Unit>(attacker);
            Team* team = world.getComponent<Team>(attacker);
            Transform* at = world.getComponent<Transform>(attacker);
            Velocity* velocity = world.getComponent<Velocity>(attacker);
            if (!unit || !team || !at || !velocity) continue;

            const Entity target =
                findTargetAhead(world, attacker, team->leftSide, at->x);

            if (target == kInvalidEntity) {
                // Nothing in reach: march. The engine does the moving.
                velocity->dx = kUnitSpeed * facing(team->leftSide);
                continue;
            }

            velocity->dx = 0.0f;
            unit->timeUntilAttack -= dt;
            if (unit->timeUntilAttack > 0.0f) continue;

            unit->timeUntilAttack = kUnitAttackDelay;

            if (Unit* victim = world.getComponent<Unit>(target)) {
                victim->health -= kUnitDamage;
                playHit();
            } else if (Castle* castle = world.getComponent<Castle>(target)) {
                castle->health -= kUnitDamage;
                playCastleHit();
                if (castle->health <= 0.0f) {
                    Team* castleTeam = world.getComponent<Team>(target);
                    session.gameOver = true;
                    // The player holds the left castle, so the right one
                    // falling is a win.
                    session.playerWon = castleTeam && !castleTeam->leftSide;
                }
            }
        }
    }

    void removeTheDead(World& world, Session& session) {
        (void)session;
        for (Entity entity : world.entities()) {
            Unit* unit = world.getComponent<Unit>(entity);
            if (!unit || unit->health > 0.0f) continue;

            if (Transform* at = world.getComponent<Transform>(entity)) {
                Sprite* sprite = world.getComponent<Sprite>(entity);
                spawnShards(world, at->x + kUnitWidth / 2.0f,
                            at->y + kUnitHeight / 2.0f,
                            sprite ? sprite->r : 200, sprite ? sprite->g : 200,
                            sprite ? sprite->b : 200);
            }
            playDeath();
            world.destroyLater(entity);
        }
    }

    // --- The view ----------------------------------------------------------

    // Where the camera wants to be: your front line, centred, and never past
    // the ends of the world.
    float cameraTargetX(World& world) const {
        const float centred = frontLineX(world, true) -
                              static_cast<float>(kWindowWidth) / 2.0f;
        return std::min(std::max(centred, 0.0f), kCameraMaxX);
    }

    void snapCameraToTarget(World& world) {
        if (Camera* camera = world.getComponent<Camera>(cameraEntity_)) {
            camera->x = cameraTargetX(world);
        }
    }

    void updateCamera(World& world, Session& session, InputManager& input,
                      float dt) {
        Camera* camera = world.getComponent<Camera>(cameraEntity_);
        if (!camera) return;

        const bool left = input.isKeyDown(SDL_SCANCODE_LEFT);
        const bool right = input.isKeyDown(SDL_SCANCODE_RIGHT);

        if (left != right) {
            camera->x += (right ? 1.0f : -1.0f) * kFreeLookSpeed * dt;
            session.freeLookSeconds = kFreeLookHold;
        } else if (session.freeLookSeconds > 0.0f) {
            // Held where the player left it, so a glance at your own castle
            // isn't yanked away the instant you let go of the key.
            session.freeLookSeconds -= dt;
        } else {
            // Exponential catch-up: it closes a fixed FRACTION of the
            // remaining distance each second, so it starts quickly and eases
            // in rather than arriving at a hard stop. Written against dt so
            // the feel does not change with the frame rate.
            const float target = cameraTargetX(world);
            camera->x += (target - camera->x) *
                         std::min(1.0f, kCameraFollowRate * dt);
        }

        // Clamped last, and unconditionally, so free-look obeys the same
        // limits as following does.
        camera->x = std::min(std::max(camera->x, 0.0f), kCameraMaxX);
        camera->y = 0.0f;
    }

    // --- The minimap -------------------------------------------------------

    void buildMinimap(World& world) {
        hud_.push_back(makeScreenRect(world, kMinimapX, kMinimapY,
                                      kMinimapWidth, kMinimapHeight, 30, 34, 44,
                                      kHudLayer));

        // Castles first so the front-line markers draw over them: same layer,
        // and within a layer the draw order is by entity id.
        minimapLeftCastle_ = makeScreenRect(world, 0.0f, kMinimapY, 4.0f,
                                            kMinimapHeight, 90, 150, 200,
                                            kHudLayer);
        minimapRightCastle_ = makeScreenRect(world, 0.0f, kMinimapY, 4.0f,
                                             kMinimapHeight, 200, 110, 100,
                                             kHudLayer);
        minimapLeftFront_ = makeScreenRect(world, 0.0f, kMinimapY - 3.0f,
                                           kMinimapMarkerWidth,
                                           kMinimapHeight + 6.0f, 140, 210, 250,
                                           kHudLayer);
        minimapRightFront_ = makeScreenRect(world, 0.0f, kMinimapY - 3.0f,
                                            kMinimapMarkerWidth,
                                            kMinimapHeight + 6.0f, 250, 150, 130,
                                            kHudLayer);

        hud_.push_back(minimapLeftCastle_);
        hud_.push_back(minimapRightCastle_);
        hud_.push_back(minimapLeftFront_);
        hud_.push_back(minimapRightFront_);
    }

    Entity makeScreenRect(World& world, float x, float y, float width,
                          float height, unsigned char r, unsigned char g,
                          unsigned char b, int layer) {
        const Entity entity =
            createRect(world, x, y, width, height, r, g, b, layer);
        world.getComponent<Sprite>(entity)->screenSpace = true;
        return entity;
    }

    void refreshMinimap(World& world) {
        placeMarker(world, minimapLeftCastle_, kLeftCastleX + kCastleWidth / 2.0f,
                    4.0f);
        placeMarker(world, minimapRightCastle_,
                    kRightCastleX + kCastleWidth / 2.0f, 4.0f);
        placeMarker(world, minimapLeftFront_, frontLineX(world, true),
                    kMinimapMarkerWidth);
        placeMarker(world, minimapRightFront_, frontLineX(world, false),
                    kMinimapMarkerWidth);
    }

    // World x -> a position along the strip, with the marker centred on it and
    // kept inside the strip at both ends.
    void placeMarker(World& world, Entity marker, float worldX, float width) {
        Transform* transform = world.getComponent<Transform>(marker);
        if (!transform) return;

        const float fraction = std::min(std::max(worldX / kWorldWidth, 0.0f), 1.0f);
        transform->x = kMinimapX + fraction * (kMinimapWidth - width);
    }

    void refreshHud(World& world, const Session& session) {
        const int gold = static_cast<int>(session.gold);
        if (gold != shownGold_) {
            if (Text* text = world.getComponent<Text>(goldText_)) {
                text->value = "GOLD " + std::to_string(gold);
            }
            shownGold_ = gold;
        }

        updateHealthText(world, leftHealthText_, "YOU  ", true);
        updateHealthText(world, rightHealthText_, "ENEMY ", false);
    }

    void updateHealthText(World& world, Entity textEntity,
                          const std::string& label, bool leftSide) {
        const Entity castle = findCastle(world, leftSide);
        Text* text = world.getComponent<Text>(textEntity);
        if (castle == kInvalidEntity || !text) return;

        const Castle* health = world.getComponent<Castle>(castle);
        if (!health) return;

        text->value = label + std::to_string(
                                  static_cast<int>(std::max(0.0f, health->health)));
    }

    Entity sessionEntity_ = kInvalidEntity;
    Entity cameraEntity_ = kInvalidEntity;
    Entity goldText_ = kInvalidEntity;
    Entity leftHealthText_ = kInvalidEntity;
    Entity rightHealthText_ = kInvalidEntity;
    Entity minimapLeftCastle_ = kInvalidEntity;
    Entity minimapRightCastle_ = kInvalidEntity;
    Entity minimapLeftFront_ = kInvalidEntity;
    Entity minimapRightFront_ = kInvalidEntity;
    std::vector<Entity> field_;
    std::vector<Entity> hud_;
    int shownGold_ = -1;
    bool overlayShown_ = false;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createCenteredText(world, "LANE BATTLE", 110, 8,
                                            220, 200, 140, kHudLayer));
        owned_.push_back(createCenteredText(world, "SEND UNITS RIGHT", 220, 2,
                                            200, 200, 215, kHudLayer));
        owned_.push_back(createCenteredText(world, "BREAK THE ENEMY CASTLE",
                                            250, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(world, "SPACE TO START", 350, 3,
                                            235, 235, 235, kHudLayer));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 400, 2,
                                            170, 170, 195, kHudLayer));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World&, InputManager& input, float, SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            scenes.replace(makePlayScene());
        } else if (input.wasKeyPressed(SDL_SCANCODE_Q)) {
            scenes.pop();  // an empty stack is how the engine is told to quit
        }
    }

private:
    std::vector<Entity> owned_;
};

}  // namespace

std::unique_ptr<Scene> makeTitleScene() {
    return std::make_unique<TitleScene>();
}

std::unique_ptr<Scene> makePlayScene() {
    return std::make_unique<PlayScene>();
}

}  // namespace lanebattle
