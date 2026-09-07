// ---------------------------------------------------------------------------
// Breakout.cpp — the third game on this engine, and the first one that needs
// collision *response*.
//
// Snake asked collision a yes/no question: did the head hit something lethal?
// Asteroids asked the same thing — a rock either explodes or it doesn't. Neither
// needed to know which *side* of a thing it touched, so `CollisionSystem` only
// ever reported "these two overlap".
//
// A ball cannot work that way. Hitting the top of a brick must flip its
// vertical speed; hitting the left face must flip its horizontal speed. That
// difference is the contact normal, and this game is what pulled `Contact`,
// the normal, the penetration depth and `reflect()` into the engine.
//
// The other thing this game forced is sub-frame movement — see moveBall().
// ---------------------------------------------------------------------------

#include "Breakout.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/Font.h"
#include "engine/Systems.h"

using namespace engine;

namespace breakout {
namespace {

constexpr int kFieldLayer = 0;
constexpr int kHudLayer = 5;
constexpr int kOverlayLayer = 10;
constexpr int kOverlayTextLayer = 11;

// Silent unless main() hands over a device.
AudioDevice* audioDevice = nullptr;

// Sound is arithmetic here, not files: see engine/Audio.h. A brick's pitch
// rises with the row it came from, which turns clearing a wall into a little
// ascending run rather than the same click sixty times.
void playBrick(int row) {
    if (!audioDevice) return;
    const float frequency = 330.0f + 55.0f * static_cast<float>(row);
    audioDevice->play(Waveform::Square, frequency, 0.05f, 0.16f);
}

void playPaddle() {
    if (audioDevice) audioDevice->play(Waveform::Square, 220.0f, 0.06f, 0.16f);
}

void playWall() {
    if (audioDevice) audioDevice->play(Waveform::Square, 160.0f, 0.04f, 0.10f);
}

void playLaunch() {
    if (audioDevice) audioDevice->play(Waveform::Sine, 520.0f, 0.09f, 0.16f);
}

void playLifeLost() {
    if (!audioDevice) return;
    audioDevice->play(Waveform::Noise, 0.0f, 0.30f, 0.22f);
    audioDevice->play(Waveform::Sine, 110.0f, 0.35f, 0.20f);
}

// How much steering the paddle imparts. Hitting the paddle off-centre angles
// the ball away from the middle, which is the whole skill of the game — a
// purely mirror-like paddle would make every rally identical.
constexpr float kPaddleSteer = 240.0f;

// --- Small helpers ---------------------------------------------------------

float ballSpeedForLevel(int level) {
    return kBallSpeed + kBallSpeedPerLevel * static_cast<float>(level - 1);
}

Vec2 scaledToSpeed(Vec2 velocity, float speed) {
    const float length = std::hypot(velocity.x, velocity.y);
    if (length < 1e-5f) return Vec2{0.0f, -speed};
    return Vec2{velocity.x / length * speed, velocity.y / length * speed};
}

// The smallest share of the ball's speed that must stay vertical.
constexpr float kMinVerticalShare = 0.30f;

// Stops the ball flattening out into an almost horizontal crawl.
//
// The paddle only ever adds sideways speed, and the total is then rescaled to
// a fixed value — so the vertical part shrinks a little with every off-centre
// hit. Land on the same side repeatedly and it compounds: 0.86 of the speed
// vertical, then 0.61, 0.39, 0.25, 0.16... until the ball is skimming between
// the side walls taking an age to come down. Forcing a minimum vertical share
// and rescaling keeps the rally moving without taking the steering away.
Vec2 keepPlayable(Vec2 velocity, float speed) {
    const float minimumVertical = speed * kMinVerticalShare;
    if (std::fabs(velocity.y) >= minimumVertical) return velocity;

    velocity.y = velocity.y < 0.0f ? -minimumVertical : minimumVertical;
    return scaledToSpeed(velocity, speed);
}

Entity createBox(World& world, float x, float y, float width, float height,
                 unsigned char r, unsigned char g, unsigned char b) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{x, y, 0.0f});

    Sprite sprite;
    sprite.width = static_cast<int>(width);
    sprite.height = static_cast<int>(height);
    sprite.r = r;
    sprite.g = g;
    sprite.b = b;
    sprite.layer = kFieldLayer;
    world.addComponent(entity, sprite);

    world.addComponent(entity, Collider{static_cast<int>(width),
                                        static_cast<int>(height)});
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

// --- Building the field ----------------------------------------------------

void createWalls(World& world, std::vector<Entity>& owned) {
    const float thickness = static_cast<float>(kWallThickness);
    const unsigned char r = 70, g = 70, b = 90;

    // Left, right and top only. The bottom is deliberately open: falling out
    // of the field is how a life is lost.
    Entity left = createBox(world, 0.0f, 0.0f, thickness,
                            static_cast<float>(kWindowHeight), r, g, b);
    Entity right = createBox(world, static_cast<float>(kWindowWidth) - thickness,
                             0.0f, thickness,
                             static_cast<float>(kWindowHeight), r, g, b);
    Entity top = createBox(world, 0.0f, 0.0f,
                           static_cast<float>(kWindowWidth), thickness, r, g, b);

    for (Entity wall : {left, right, top}) {
        world.addComponent(wall, Wall{});
        owned.push_back(wall);
    }
}

void createBricks(World& world) {
    const float width = brickWidth();

    for (int row = 0; row < kBrickRows; ++row) {
        // A colour ramp down the rows, so the rows read as distinct without
        // any artwork.
        const unsigned char red = static_cast<unsigned char>(210 - row * 22);
        const unsigned char green = static_cast<unsigned char>(90 + row * 24);
        const unsigned char blue = static_cast<unsigned char>(110 + row * 16);

        for (int column = 0; column < kBrickColumns; ++column) {
            const float x = static_cast<float>(kWallThickness + kFieldMargin) +
                            static_cast<float>(column) *
                                (width + static_cast<float>(kBrickGap));
            const float y = static_cast<float>(kBrickTop + row *
                                               (kBrickHeight + kBrickGap));

            Entity brick = createBox(world, x, y, width,
                                     static_cast<float>(kBrickHeight), red,
                                     green, blue);
            world.addComponent(brick, Brick{row});
        }
    }
}

Entity createPaddle(World& world) {
    Entity paddle = createBox(world,
                              (static_cast<float>(kWindowWidth) - kPaddleWidth) / 2.0f,
                              kPaddleY, kPaddleWidth, kPaddleHeight,
                              215, 215, 230);
    world.addComponent(paddle, Paddle{});
    return paddle;
}

Entity createBall(World& world) {
    Entity ball = world.createEntity();
    world.addComponent(ball, Transform{static_cast<float>(kWindowWidth) / 2.0f,
                                       kPaddleY - kBallRadius - 2.0f, 0.0f});
    world.addComponent(ball, CircleCollider{kBallRadius});

    // Drawn as a Polygon rather than a Sprite, because a Polygon's points are
    // centred on the Transform exactly like the circle collider is. A square
    // Sprite hangs down-right from the Transform instead, so it would sit half
    // a ball off from the thing that actually collides.
    Polygon outline;
    const int sides = 10;
    for (int i = 0; i < sides; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) /
                            static_cast<float>(sides);
        outline.points.push_back(Vec2{std::cos(angle) * kBallRadius,
                                      std::sin(angle) * kBallRadius});
    }
    outline.r = 255;
    outline.g = 240;
    outline.b = 190;
    outline.layer = kFieldLayer;
    world.addComponent(ball, outline);

    world.addComponent(ball, Ball{});
    return ball;
}

// --- Ball movement ---------------------------------------------------------

// Moves the ball and handles whatever it runs into.
//
// The ball is integrated HERE rather than by the engine's MovementSystem, and
// that is the point of this function. MovementSystem takes one jump per frame:
// at 400 px/s and 60fps that is nearly 7 pixels, and at 30fps it is 13 — more
// than half the height of a 24-pixel brick. A single jump can start above a
// brick and end below it, overlapping nothing at either end, and the ball
// sails straight through. That is "tunnelling", and every engine meets it.
//
// The fix is to take the frame's movement in steps no longer than kMaxSubstep
// and test after each one. Cheap, and it makes the behaviour independent of
// frame rate, which a one-jump-per-frame integrator can never be.
void moveBall(World& world, Entity ballEntity, Session& session, float dt) {
    Ball* ball = world.getComponent<Ball>(ballEntity);
    Transform* transform = world.getComponent<Transform>(ballEntity);
    if (!ball || !transform || ball->stuckToPaddle) return;

    const float distance = std::hypot(ball->velocity.x, ball->velocity.y) * dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / kMaxSubstep)));
    const float stepDt = dt / static_cast<float>(steps);

    // Bricks destroyed earlier in this frame are still alive — deletion is
    // deferred to the end of it — so without remembering them, a brick the
    // ball is still overlapping on the next substep would be scored a second
    // time and click twice. Tracked across the whole move, not per substep.
    std::unordered_set<Entity> alreadyBroken;

    for (int step = 0; step < steps; ++step) {
        transform->x += ball->velocity.x * stepDt;
        transform->y += ball->velocity.y * stepDt;

        // Only the ball moves, so asking CollisionSystem for every pair in the
        // world would compare seventy bricks against each other sixty times a
        // second for nothing. Testing the ball against each collider is the
        // same answer for a fraction of the work.
        Contact deepest;
        Entity hit = kInvalidEntity;
        std::vector<Entity> bricksHit;

        for (Entity other : world.entities()) {
            if (other == ballEntity) continue;
            if (!world.hasComponent<Collider>(other)) continue;

            const Contact contact = contactBetween(world, ballEntity, other);
            if (!contact.overlapping) continue;

            if (world.hasComponent<Brick>(other) &&
                alreadyBroken.find(other) == alreadyBroken.end()) {
                bricksHit.push_back(other);
            }

            // Respond to the deepest contact only. Clipping the corner where
            // two bricks meet touches both, and reflecting twice in one step
            // would send the ball back the way it came.
            if (!deepest.overlapping || contact.depth > deepest.depth) {
                deepest = contact;
                hit = other;
            }
        }

        for (Entity brick : bricksHit) {
            if (Brick* data = world.getComponent<Brick>(brick)) playBrick(data->row);
            alreadyBroken.insert(brick);
            world.destroyLater(brick);
            session.score += kBrickScore;
        }

        if (hit == kInvalidEntity) continue;

        if (world.hasComponent<Paddle>(hit)) {
            playPaddle();
        } else if (world.hasComponent<Wall>(hit)) {
            playWall();
        }

        // Lift the ball back out of whatever it sank into, then mirror its
        // velocity about the surface it touched.
        transform->x -= deepest.normal.x * deepest.depth;
        transform->y -= deepest.normal.y * deepest.depth;
        ball->velocity = reflect(ball->velocity, deepest.normal);

        // The paddle is not a mirror: where the ball lands across its face
        // angles the bounce, which is what lets a player aim.
        if (world.hasComponent<Paddle>(hit)) {
            const Transform* paddle = world.getComponent<Transform>(hit);
            const float center = paddle->x + kPaddleWidth / 2.0f;
            const float offset = (transform->x - center) / (kPaddleWidth / 2.0f);
            ball->velocity.x += offset * kPaddleSteer;
            const float speed = ballSpeedForLevel(session.level);
            ball->velocity = keepPlayable(scaledToSpeed(ball->velocity, speed),
                                          speed);
        }
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

Entity findBall(World& world) {
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Ball>(entity)) return entity;
    }
    return kInvalidEntity;
}

Entity findPaddle(World& world) {
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Paddle>(entity)) return entity;
    }
    return kInvalidEntity;
}

int countBricks(World& world) {
    int count = 0;
    for (Entity entity : world.entities()) {
        if (world.hasComponent<Brick>(entity)) ++count;
    }
    return count;
}

namespace {

// --- Scenes ----------------------------------------------------------------

// There is no win state, and that is faithful: the original Breakout simply
// refills the field and speeds the ball up until you run out of lives.
class GameOverScene : public Scene {
public:
    explicit GameOverScene(int score) : score_(score) {}

    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "GAME OVER", 220, 6,
                                            240, 110, 110, kOverlayTextLayer));
        owned_.push_back(createCenteredText(
            world, "SCORE: " + std::to_string(score_), 310, 3, 235, 235, 235,
            kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "R TO PLAY AGAIN", 380, 2,
                                            170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 410, 2,
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
    std::vector<Entity> owned_;
};

class PauseScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "PAUSED", 250, 6,
                                            235, 235, 235, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "P TO RESUME", 340, 2,
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

        createWalls(world, owned_);
        paddle_ = createPaddle(world);
        ball_ = createBall(world);
        createBricks(world);

        hudScore_ = createText(world, "SCORE: 0", 24, 24, 3, 200, 200, 215,
                               kHudLayer);
        hudLives_ = createText(world, "LIVES: 3", kWindowWidth - 190, 24, 3,
                               200, 200, 215, kHudLayer);
    }

    void onExit(World& world) override {
        for (Entity entity : world.entities()) {
            if (world.hasComponent<Brick>(entity)) world.destroyLater(entity);
        }
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();

        world.destroyLater(paddle_);
        world.destroyLater(ball_);
        world.destroyLater(hudScore_);
        world.destroyLater(hudLives_);
        world.destroyLater(sessionEntity_);
    }

    void onResume(World& world) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session || !session->gameOver) return;  // returning from pause

        for (Entity entity : world.entities()) {
            if (world.hasComponent<Brick>(entity)) world.destroyLater(entity);
        }
        *session = Session{};
        createBricks(world);
        resetBall(world);
        shownScore_ = -1;
        shownLives_ = -1;
        overlayShown_ = false;
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        Session* session = world.getComponent<Session>(sessionEntity_);
        if (!session) return;

        if (session->gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(session->score));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        movePaddle(world, input, dt);
        launchOrCarryBall(world, input, *session);
        moveBall(world, ball_, *session, dt);
        checkBallLost(world, *session);
        checkFieldCleared(world, *session);
        refreshHud(world, *session);
    }

private:
    void movePaddle(World& world, InputManager& input, float dt) {
        Transform* transform = world.getComponent<Transform>(paddle_);
        if (!transform) return;

        if (input.isKeyDown(SDL_SCANCODE_LEFT)) transform->x -= kPaddleSpeed * dt;
        if (input.isKeyDown(SDL_SCANCODE_RIGHT)) transform->x += kPaddleSpeed * dt;

        const float minX = static_cast<float>(kWallThickness);
        const float maxX = static_cast<float>(kWindowWidth - kWallThickness) -
                           kPaddleWidth;
        transform->x = std::max(minX, std::min(transform->x, maxX));
    }

    // Before launch the ball rides on the paddle, so the player chooses where
    // the rally starts.
    void launchOrCarryBall(World& world, InputManager& input, Session& session) {
        Ball* ball = world.getComponent<Ball>(ball_);
        Transform* ballAt = world.getComponent<Transform>(ball_);
        Transform* paddleAt = world.getComponent<Transform>(paddle_);
        if (!ball || !ballAt || !paddleAt) return;

        if (!ball->stuckToPaddle) return;

        ballAt->x = paddleAt->x + kPaddleWidth / 2.0f;
        ballAt->y = kPaddleY - kBallRadius - 2.0f;

        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            ball->stuckToPaddle = false;
            // Up and slightly to the right: a fixed opening angle, so every
            // round starts the same way and a test can predict it.
            ball->velocity = scaledToSpeed(Vec2{0.45f, -1.0f},
                                           ballSpeedForLevel(session.level));
            playLaunch();
        }
    }

    void resetBall(World& world) {
        Ball* ball = world.getComponent<Ball>(ball_);
        if (ball) {
            ball->stuckToPaddle = true;
            ball->velocity = Vec2{};
        }
    }

    // The bottom of the field has no wall, so a missed ball simply leaves.
    void checkBallLost(World& world, Session& session) {
        Transform* ballAt = world.getComponent<Transform>(ball_);
        if (!ballAt) return;
        if (ballAt->y - kBallRadius < static_cast<float>(kWindowHeight)) return;

        playLifeLost();
        --session.lives;
        if (session.lives <= 0) {
            session.gameOver = true;
            return;
        }
        resetBall(world);
    }

    void checkFieldCleared(World& world, Session& session) {
        if (countBricks(world) > 0) return;

        ++session.level;
        createBricks(world);
        resetBall(world);
    }

    void refreshHud(World& world, const Session& session) {
        if (session.score != shownScore_) {
            if (Text* text = world.getComponent<Text>(hudScore_)) {
                text->value = "SCORE: " + std::to_string(session.score);
            }
            shownScore_ = session.score;
        }
        if (session.lives != shownLives_) {
            if (Text* text = world.getComponent<Text>(hudLives_)) {
                text->value = "LIVES: " + std::to_string(session.lives);
            }
            shownLives_ = session.lives;
        }
    }

    Entity sessionEntity_ = kInvalidEntity;
    Entity paddle_ = kInvalidEntity;
    Entity ball_ = kInvalidEntity;
    Entity hudScore_ = kInvalidEntity;
    Entity hudLives_ = kInvalidEntity;
    std::vector<Entity> owned_;
    int shownScore_ = -1;
    int shownLives_ = -1;
    bool overlayShown_ = false;
};

class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createCenteredText(world, "BREAKOUT", 150, 9,
                                            230, 190, 120, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "ARROWS MOVE THE PADDLE",
                                            300, 2, 200, 200, 215, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "SPACE LAUNCHES - P PAUSES",
                                            330, 2, 170, 170, 195, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "SPACE TO START", 430, 3,
                                            235, 235, 235, kOverlayTextLayer));
        owned_.push_back(createCenteredText(world, "Q TO QUIT", 480, 2,
                                            170, 170, 195, kOverlayTextLayer));
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

void setAudioDevice(AudioDevice* audio) { audioDevice = audio; }

std::unique_ptr<Scene> makeTitleScene() {
    return std::make_unique<TitleScene>();
}

std::unique_ptr<Scene> makePlayScene() {
    return std::make_unique<PlayScene>();
}

}  // namespace breakout
