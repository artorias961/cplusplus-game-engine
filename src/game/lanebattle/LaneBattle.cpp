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
constexpr float kRightSpawnX = kRightCastleX - kMaxUnitWidth - 4.0f;

// Clamped so a bad index can never index off the end of the table. Every read
// of a unit's stats goes through here.
const UnitKind& kindOf(int kind) {
    if (kind < 0) return kUnitKinds[0];
    if (kind >= kUnitKindCount) return kUnitKinds[kUnitKindCount - 1];
    return kUnitKinds[kind];
}

const UnitKind& kindOf(World& world, Entity entity) {
    const Unit* unit = world.getComponent<Unit>(entity);
    return kindOf(unit ? unit->kind : 1);
}

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

// The stick figure that goes over a unit's block. Drawn a shade darker than
// the block so the limbs read against it.
//
// It carries no points yet — animateUnits fills them in on the first frame,
// which keeps the shape of the figure in exactly one place instead of two that
// have to agree.
//
// It draws on top of the block because it is created after it: within a layer
// the renderer sorts by entity id, and ids increase in creation order. That is
// a real guarantee rather than luck, but it is a quiet one, so: creating the
// figure before its unit would put the limbs behind the silhouette.
Entity createFigure(World& world, Entity owner, const Sprite& body) {
    Entity figure = world.createEntity();
    world.addComponent(figure, Transform{0.0f, 0.0f, 0.0f});
    world.addComponent(figure, Figure{owner});

    Polygon lines;
    lines.closed = false;
    lines.r = static_cast<unsigned char>(body.r / 3);
    lines.g = static_cast<unsigned char>(body.g / 3);
    lines.b = static_cast<unsigned char>(body.b / 3);
    lines.layer = kFieldLayer;
    world.addComponent(figure, lines);

    return figure;
}

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
                       float attackerX, const UnitKind& stats) {
    const float direction = facing(leftSide);
    const float attackerCenter = attackerX + stats.width / 2.0f;

    Entity best = kInvalidEntity;
    float bestGap = stats.range;

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

        const float gap = centreAhead - (stats.width + targetWidth) / 2.0f;
        if (gap > bestGap) continue;

        bestGap = gap;
        best = other;
    }
    return best;
}

// Is a friendly standing in the way?
//
// Without this the whole army occupies one pixel and fights as a single
// enormous unit, which is exactly why massing was unconditionally correct in
// slice 2. Queuing means only the front few are ever in contact, so a tenth
// unit is worth much less than a second one — the diminishing return that
// makes composition matter more than count.
//
// Two details carry all the behaviour:
//
//   - Only STOPPED friendlies block. A column in transit flows freely, so a
//     fast runner can overtake a marching soldier; the queue only forms where
//     the fighting is.
//   - A friendly only blocks you if its reach is no longer than yours. That is
//     what lets a soldier walk past its own archers to reach the enemy, while
//     archers — which stop far short — never obstruct anybody. Without that
//     one comparison, the first archer sent would wall in every melee unit
//     behind it and ranged units would be a trap rather than a support.
bool blockedByFriendly(World& world, Entity mover, bool leftSide, float moverX,
                       const UnitKind& stats) {
    const float direction = facing(leftSide);
    const float moverCenter = moverX + stats.width / 2.0f;

    for (Entity other : world.entities()) {
        if (other == mover) continue;

        Unit* otherUnit = world.getComponent<Unit>(other);
        Team* team = world.getComponent<Team>(other);
        if (!otherUnit || !team || team->leftSide != leftSide) continue;

        const UnitKind& otherStats = kindOf(otherUnit->kind);
        if (otherStats.range > stats.range) continue;  // it stops well short of me

        Velocity* velocity = world.getComponent<Velocity>(other);
        if (!velocity || std::fabs(velocity->dx) > 0.01f) continue;  // still moving

        Transform* transform = world.getComponent<Transform>(other);
        if (!transform) continue;

        const float otherCenter = transform->x + otherStats.width / 2.0f;
        const float centreAhead = (otherCenter - moverCenter) * direction;
        if (centreAhead < 0.0f) continue;  // behind me

        const float gap = centreAhead - (stats.width + otherStats.width) / 2.0f;
        if (gap < kRankGap) return true;
    }
    return false;
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
    return countUnitsOfKind(world, leftSide, -1);
}

int countUnitsOfKind(World& world, bool leftSide, int kind) {
    int count = 0;
    for (Entity entity : world.entities()) {
        Unit* unit = world.getComponent<Unit>(entity);
        if (!unit) continue;
        if (kind >= 0 && unit->kind != kind) continue;
        Team* team = world.getComponent<Team>(entity);
        if (team && team->leftSide == leftSide) ++count;
    }
    return count;
}

int countFigures(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Figure>(entity)) ++count;
    }
    return count;
}

void animateUnits(World& world, float dt) {
    for (auto& [entity, figure] : world.view<Figure>()) {
        Unit* unit = world.getComponent<Unit>(figure.owner);
        Team* team = world.getComponent<Team>(figure.owner);
        Transform* body = world.getComponent<Transform>(figure.owner);
        Velocity* velocity = world.getComponent<Velocity>(figure.owner);

        // An orphan. A death already takes its own figure with it, so this is
        // the safety net rather than the mechanism: it catches any future path
        // that makes a unit disappear without going through removeTheDead.
        if (!unit || !team || !body || !velocity) {
            world.destroyLater(entity);
            continue;
        }

        const UnitKind& stats = kindOf(unit->kind);
        const float direction = facing(team->leftSide);

        // The walk cycle advances with DISTANCE, not with time, so a runner's
        // legs move faster than a soldier's without either being told to and
        // nothing ever slides along with its feet still.
        unit->phase += std::fabs(velocity->dx) * dt * kWalkCycleRate;
        unit->swing = std::max(0.0f, unit->swing - dt * kSwingDecayRate);

        Transform* at = world.getComponent<Transform>(entity);
        Polygon* lines = world.getComponent<Polygon>(entity);
        if (!at || !lines) continue;

        // The figure hangs off the bottom centre of the block it decorates.
        at->x = body->x + stats.width / 2.0f;
        at->y = kGroundY;

        const float hipY = -stats.height * 0.45f;
        const float shoulderY = -stats.height * 0.78f;
        const float step = std::sin(unit->phase) * kLegSwing;

        // The arm sweeps from raised to lowered as the swing plays out, and
        // rests slightly forward when idle.
        const float armAngle = unit->swing > 0.0f
                                   ? (-1.1f + 1.9f * (1.0f - unit->swing))
                                   : 0.25f;
        const float reach = stats.range > 100.0f ? 10.0f : 16.0f;  // a bow is held closer
        const float handX = std::cos(armAngle) * reach * direction;
        const float handY = shoulderY + std::sin(armAngle) * reach;

        // One unbroken stroke, retracing the hip: foot, hip, other foot, back
        // up through the hip to the shoulder, then out along the arm. Retracing
        // costs one duplicated line and saves needing a second entity.
        lines->points.clear();
        lines->points.push_back(Vec2{step * direction, 0.0f});
        lines->points.push_back(Vec2{0.0f, hipY});
        lines->points.push_back(Vec2{-step * direction, 0.0f});
        lines->points.push_back(Vec2{0.0f, hipY});
        lines->points.push_back(Vec2{0.0f, shoulderY});
        lines->points.push_back(Vec2{handX, handY});
    }
}

int buttonAt(float screenX, float screenY) {
    if (screenY < kButtonY || screenY > kButtonY + kButtonHeight) return -1;

    for (int index = 0; index < kUnitKindCount; ++index) {
        const float left = buttonLeft(index);
        if (screenX >= left && screenX <= left + kButtonWidth) return index;
    }
    return -1;
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

Entity spawnUnit(World& world, bool leftSide, int kind) {
    const UnitKind& stats = kindOf(kind);
    const float x = leftSide ? kLeftSpawnX : kRightSpawnX;

    Entity unit = world.createEntity();
    world.addComponent(unit, Transform{x, kGroundY - stats.height, 0.0f});
    world.addComponent(unit, Velocity{stats.speed * facing(leftSide), 0.0f});

    Sprite sprite;
    sprite.width = static_cast<int>(stats.width);
    sprite.height = static_cast<int>(stats.height);
    if (leftSide) {
        sprite.r = stats.leftR; sprite.g = stats.leftG; sprite.b = stats.leftB;
    } else {
        sprite.r = stats.rightR; sprite.g = stats.rightG; sprite.b = stats.rightB;
    }
    sprite.layer = kFieldLayer;
    world.addComponent(unit, sprite);

    world.addComponent(unit, Team{leftSide});

    Unit component;
    component.kind = kind;
    component.health = stats.health;  // the table is the only source of health
    world.addComponent(unit, component);
    world.getComponent<Unit>(unit)->figure = createFigure(world, unit, sprite);

    // A different pitch per kind, so you can hear what you just sent without
    // looking away from the front line.
    if (audioDevice) {
        audioDevice->play(Waveform::Square, 240.0f + 60.0f * static_cast<float>(kind),
                          0.05f, 0.12f);
    }
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
        popText_ = createText(world, "", 16, 78, 2, 170, 170, 195, kHudLayer);
        hud_.push_back(popText_);

        buildSpawnBar(world);

        hud_.push_back(createText(world, "DRAG OR ARROWS TO LOOK - P PAUSES",
                                  static_cast<int>(kButtonX),
                                  static_cast<int>(kButtonY) - 22, 2,
                                  130, 130, 155, kHudLayer));

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
        shownPopulation_ = -1;
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
        animateUnits(world, dt);
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
            // Figures go explicitly rather than being left to the orphan
            // sweep: onExit is the one path where no further frame runs, so
            // there would be nothing left to sweep them.
            if (world.hasComponent<Unit>(entity) ||
                world.hasComponent<Shard>(entity) ||
                world.hasComponent<Figure>(entity)) {
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
        handlePlayerSpawning(world, session, input, dt);
        handleEnemySpawning(world, session, dt);
    }

    void handlePlayerSpawning(World& world, Session& session,
                              InputManager& input, float dt) {
        session.spawnCooldown -= dt;
        if (session.spawnCooldown > 0.0f) return;
        if (countUnits(world, true) >= kPopulationCap) return;

        // One key per row of the table, so adding a fourth unit type is a
        // table row and one scancode rather than a change to any rule.
        static const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2,
                                            SDL_SCANCODE_3, SDL_SCANCODE_4};
        for (int kind = 0; kind < kUnitKindCount && kind < 4; ++kind) {
            if (!input.isKeyDown(keys[kind])) continue;
            if (trySpawn(world, session, kind)) return;
        }

        // A click on the bar does exactly what its key does. `wasMousePressed`
        // rather than `isMouseDown`, or one held click would empty the purse
        // one unit per cooldown for as long as the finger stayed down.
        if (!input.wasMousePressed()) return;

        const int kind = buttonAt(static_cast<float>(input.mouseX()),
                                  static_cast<float>(input.mouseY()));
        if (kind >= 0) trySpawn(world, session, kind);
    }

    // Buys one unit if it can be afforded, and reports whether it did. The one
    // place gold is spent, so the keyboard and the mouse cannot drift apart.
    bool trySpawn(World& world, Session& session, int kind) {
        if (kind < 0 || kind >= kUnitKindCount) return false;
        if (session.gold < kUnitKinds[kind].cost) return false;

        session.gold -= kUnitKinds[kind].cost;
        session.spawnCooldown = kSpawnCooldown;
        spawnUnit(world, true, kind);
        return true;
    }

    // The opponent, playing by the same rules from the same purse.
    //
    // It banks for a whole wave before spending any of it, because measuring
    // slice 2 showed that spending on sight loses to anyone who doesn't: the
    // two front lines mirror each other and nothing ever moves. Now that both
    // sides bank, the player has to beat it on composition instead.
    void handleEnemySpawning(World& world, Session& session, float dt) {
        session.enemySpawnTimer -= dt;

        if (session.enemyWaveRemaining <= 0) {
            if (session.enemyGold >= waveCost(session)) {
                session.enemyWaveRemaining = kEnemyWaveSize;
            }
            return;
        }

        if (session.enemySpawnTimer > 0.0f) return;
        if (countUnits(world, false) >= kPopulationCap) return;

        const int kind = kEnemyComposition[session.enemyWaveIndex %
                                           kEnemyCompositionLength];
        if (session.enemyGold < kUnitKinds[kind].cost) return;

        session.enemyGold -= kUnitKinds[kind].cost;
        session.enemySpawnTimer = kSpawnCooldown;
        session.enemyWaveIndex =
            (session.enemyWaveIndex + 1) % kEnemyCompositionLength;
        --session.enemyWaveRemaining;
        spawnUnit(world, false, kind);
    }

    // What the next whole wave costs, reading forward through the cycle from
    // wherever it currently is.
    static float waveCost(const Session& session) {
        float total = 0.0f;
        for (int step = 0; step < kEnemyWaveSize; ++step) {
            const int index = (session.enemyWaveIndex + step) %
                              kEnemyCompositionLength;
            total += kUnitKinds[kEnemyComposition[index]].cost;
        }
        return total;
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

            const UnitKind& stats = kindOf(unit->kind);
            const Entity target =
                findTargetAhead(world, attacker, team->leftSide, at->x, stats);

            if (target == kInvalidEntity) {
                // Nothing in reach. March, unless one of our own is in the
                // way — in which case wait our turn rather than standing
                // inside them.
                const bool queued = blockedByFriendly(world, attacker,
                                                      team->leftSide, at->x, stats);
                velocity->dx = queued ? 0.0f : stats.speed * facing(team->leftSide);
                continue;
            }

            velocity->dx = 0.0f;
            unit->timeUntilAttack -= dt;
            if (unit->timeUntilAttack > 0.0f) continue;

            unit->timeUntilAttack = stats.attackDelay;
            unit->swing = 1.0f;  // starts the arm through its arc

            if (Unit* victim = world.getComponent<Unit>(target)) {
                victim->health -= stats.damage;
                playHit();
            } else if (Castle* castle = world.getComponent<Castle>(target)) {
                castle->health -= stats.damage;
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
        for (Entity entity : world.entities()) {
            Unit* unit = world.getComponent<Unit>(entity);
            if (!unit || unit->health > 0.0f) continue;

            // Whoever killed it gets paid. The bounty is a share of what the
            // casualty cost its owner, so trading cheap units for expensive
            // ones is profitable and trading the other way is not.
            const float bounty = kindOf(unit->kind).cost * kKillRewardFraction;
            if (Team* team = world.getComponent<Team>(entity)) {
                if (team->leftSide) {
                    session.enemyGold += bounty;
                } else {
                    session.gold += bounty;
                }
            }

            if (Transform* at = world.getComponent<Transform>(entity)) {
                const UnitKind& stats = kindOf(unit->kind);
                Sprite* sprite = world.getComponent<Sprite>(entity);
                spawnShards(world, at->x + stats.width / 2.0f,
                            at->y + stats.height / 2.0f,
                            sprite ? sprite->r : 200, sprite ? sprite->g : 200,
                            sprite ? sprite->b : 200);
            }
            playDeath();
            world.destroyLater(entity);
            if (unit->figure != kInvalidEntity) world.destroyLater(unit->figure);
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

        // A drag on the field scrolls the view. Starting one over the spawn
        // bar is not a drag — that press belongs to the button underneath it,
        // or every click on a button would also nudge the camera.
        if (input.wasMousePressed() &&
            buttonAt(static_cast<float>(input.mouseX()),
                     static_cast<float>(input.mouseY())) < 0) {
            session.dragging = true;
            session.dragStartX = static_cast<float>(input.mouseX());
            session.dragStartCameraX = camera->x;
        }
        if (!input.isMouseDown()) session.dragging = false;

        if (session.dragging) {
            // The field follows the cursor: drag left and the world moves
            // left under your finger, which means the camera moves right.
            const float moved =
                static_cast<float>(input.mouseX()) - session.dragStartX;
            if (std::fabs(moved) >= kDragThreshold) {
                camera->x = session.dragStartCameraX - moved;
                session.freeLookSeconds = kFreeLookHold;
                camera->x = std::min(std::max(camera->x, 0.0f), kCameraMaxX);
                camera->y = 0.0f;
                return;
            }
        }

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

    // --- The spawn bar -----------------------------------------------------

    void buildSpawnBar(World& world) {
        for (int kind = 0; kind < kUnitKindCount; ++kind) {
            const UnitKind& stats = kUnitKinds[kind];
            const float left = buttonLeft(kind);

            // Three pieces per button: the plate, a fill that shrinks as the
            // cooldown runs, and the label. The fill is drawn over the plate
            // and under the text, which the layer sort already guarantees
            // because entity ids increase in creation order within a layer.
            buttonPlate_[kind] =
                makeScreenRect(world, left, kButtonY, kButtonWidth,
                               kButtonHeight, 34, 38, 48, kHudLayer);
            buttonFill_[kind] =
                makeScreenRect(world, left, kButtonY + kButtonHeight - 4.0f,
                               kButtonWidth, 4.0f, stats.leftR, stats.leftG,
                               stats.leftB, kHudLayer);

            const std::string label =
                std::to_string(kind + 1) + " " + stats.name;
            buttonName_[kind] = createText(
                world, label, static_cast<int>(left) + 8,
                static_cast<int>(kButtonY) + 8, 2, stats.leftR, stats.leftG,
                stats.leftB, kHudLayer);
            buttonCost_[kind] = createText(
                world, std::to_string(static_cast<int>(stats.cost)),
                static_cast<int>(left) + 8, static_cast<int>(kButtonY) + 26, 2,
                220, 200, 140, kHudLayer);

            world.getComponent<Text>(buttonName_[kind])->screenSpace = true;
            world.getComponent<Text>(buttonCost_[kind])->screenSpace = true;

            hud_.push_back(buttonPlate_[kind]);
            hud_.push_back(buttonFill_[kind]);
            hud_.push_back(buttonName_[kind]);
            hud_.push_back(buttonCost_[kind]);
        }
    }

    void refreshSpawnBar(World& world, const Session& session, int fielded) {
        for (int kind = 0; kind < kUnitKindCount; ++kind) {
            const UnitKind& stats = kUnitKinds[kind];
            const bool affordable = session.gold >= stats.cost &&
                                    fielded < kPopulationCap;

            if (Text* name = world.getComponent<Text>(buttonName_[kind])) {
                name->r = affordable ? stats.leftR : 95;
                name->g = affordable ? stats.leftG : 95;
                name->b = affordable ? stats.leftB : 110;
            }
            if (Text* cost = world.getComponent<Text>(buttonCost_[kind])) {
                cost->r = affordable ? 220 : 95;
                cost->g = affordable ? 200 : 95;
                cost->b = affordable ? 140 : 110;
            }
            if (Sprite* plate = world.getComponent<Sprite>(buttonPlate_[kind])) {
                plate->r = affordable ? 44 : 28;
                plate->g = affordable ? 50 : 32;
                plate->b = affordable ? 64 : 40;
            }

            // The fill is the shared spawn cooldown draining left to right,
            // so the bar shows *when* you can send as well as *what*.
            if (Sprite* fill = world.getComponent<Sprite>(buttonFill_[kind])) {
                const float remaining =
                    std::max(0.0f, session.spawnCooldown) / kSpawnCooldown;
                fill->width = static_cast<int>(kButtonWidth * (1.0f - remaining));
            }
        }
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

        const int fielded = countUnits(world, true);
        if (fielded != shownPopulation_) {
            if (Text* text = world.getComponent<Text>(popText_)) {
                text->value = "UNITS " + std::to_string(fielded) + "/" +
                              std::to_string(kPopulationCap);
            }
            shownPopulation_ = fielded;
        }

        // Dim whatever you cannot currently buy, so affordability is readable
        // without doing arithmetic against the gold counter.
        refreshSpawnBar(world, session, fielded);

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
    Entity popText_ = kInvalidEntity;
    Entity buttonPlate_[kUnitKindCount] = {};
    Entity buttonFill_[kUnitKindCount] = {};
    Entity buttonName_[kUnitKindCount] = {};
    Entity buttonCost_[kUnitKindCount] = {};
    Entity leftHealthText_ = kInvalidEntity;
    Entity rightHealthText_ = kInvalidEntity;
    Entity minimapLeftCastle_ = kInvalidEntity;
    Entity minimapRightCastle_ = kInvalidEntity;
    Entity minimapLeftFront_ = kInvalidEntity;
    Entity minimapRightFront_ = kInvalidEntity;
    std::vector<Entity> field_;
    std::vector<Entity> hud_;
    int shownGold_ = -1;
    int shownPopulation_ = -1;
    bool overlayShown_ = false;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createCenteredText(world, "LANE BATTLE", 110, 8,
                                            220, 200, 140, kHudLayer));
        owned_.push_back(createCenteredText(world, "1 RUNNER  2 SOLDIER  3 ARCHER",
                                            210, 2, 200, 200, 215, kHudLayer));
        owned_.push_back(createCenteredText(world, "ARCHERS NEED A FRONT LINE",
                                            240, 2, 170, 170, 195, kHudLayer));
        owned_.push_back(createCenteredText(world, "BREAK THE ENEMY CASTLE",
                                            270, 2, 170, 170, 195, kHudLayer));
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
