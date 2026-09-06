// ---------------------------------------------------------------------------
// main.cpp — Snake, built on the engine in include/engine/.
//
// Everything in this file is game-specific: how big a grid cell is, how fast
// the snake steps, what a "segment" is, what happens when the head touches
// food. None of that leaks into engine/ — the engine only ever sees
// Transforms, Sprites, Colliders, Text, and scenes it updates one at a time.
//
// The engine features this game leans on, and why:
//
//   1. AABB collision (engine/Systems.h). Food, the walls, and the snake's
//      own body are all rectangles with a Collider, so one generic overlap
//      test answers all three of the questions this game asks.
//   2. Tick-based movement (engine/Timing.h). The engine's MovementSystem
//      moves things smoothly every frame (Transform += Velocity * dt). Snake
//      instead sits still and jumps one whole cell every kTickSeconds, which
//      is what keeps everything grid-aligned. Nothing here has a Velocity at
//      all, so MovementSystem runs every frame and finds nothing to do.
//   3. Scenes (engine/Scene.h). Title, playing, paused and game-over are four
//      objects on a stack rather than four booleans checked in every update.
//   4. Text (engine/Components.h, engine/Font.h). The score and every menu
//      label is an entity with a Text component, drawn by the same renderer
//      that draws the snake.
//   5. Textures (engine/Resources.h). The snake and the food are tiles cut
//      out of one PNG; the walls are still plain colored rectangles, because
//      a Sprite can be either. If the image is missing, everything falls
//      back to rectangles and the game plays exactly the same.
// ---------------------------------------------------------------------------

#include <SDL.h>

#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "engine/Scene.h"
#include "engine/Systems.h"
#include "engine/Timing.h"

using namespace engine;

namespace {

// --- Tuning knobs ----------------------------------------------------------

constexpr int kCellSize = 32;  // pixels per grid cell
constexpr int kGridCols = 24;  // total columns, including the wall border
constexpr int kGridRows = 18;  // total rows, including the wall border

// The window is sized from the grid rather than the other way around, so
// changing kCellSize or the grid dimensions can't leave a partial cell
// hanging off the edge.
constexpr int kWindowWidth = kCellSize * kGridCols;
constexpr int kWindowHeight = kCellSize * kGridRows;

// How long the snake waits between moves. Note how much longer this is than
// a frame (~16ms at 60fps): roughly seven frames pass between steps, and
// nothing on the board moves during them.
constexpr float kTickSeconds = 0.12f;

constexpr int kStartLength = 4;  // segments, head included

// One PNG holds three 32x32 tiles side by side: head, body, food. Drawing
// from a sheet rather than three files means one load, one texture, and one
// place to add the next frame of animation.
constexpr int kTileSize = 32;
constexpr int kHeadTile = 0;
constexpr int kBodyTile = 1;
constexpr int kFoodTile = 2;

// Draw layers. The board sits at 0; the dimming panel behind a pause or
// game-over message sits above it. Text always draws above every sprite.
constexpr int kBoardLayer = 0;
constexpr int kOverlayLayer = 10;

// The sprite sheet, loaded once in main(). A larger game would gather its
// textures into an Assets struct and hand that to each scene; with exactly
// one image, a file-scope pointer is the honest amount of machinery.
SDL_Texture* spriteSheet = nullptr;

// --- Game-specific components ----------------------------------------------
//
// World stores components keyed by their C++ type, so game code can invent
// its own without touching engine/ at all — these three types are unknown to
// the engine and it works with them anyway.
//
// They're pure tags with no fields. Their only job is to let the game ask
// "what did the head just run into?" and get a meaningful answer back, which
// beats keeping three separate lists of entity IDs in sync by hand.
struct SnakeSegment {};
struct Food {};
struct Wall {};

// --- Grid helpers ----------------------------------------------------------

// A position in grid cells, not pixels. The game thinks entirely in cells;
// pixels only appear at the moment a cell is written into a Transform for
// the engine to draw. Integer cells also mean "same square?" is an exact
// comparison, with none of the fuzziness of comparing floats.
struct Cell {
    int x = 0;
    int y = 0;
};

bool sameCell(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }

// The fallback look: a flat rectangle 2px smaller than its cell, so
// neighboring segments stay visually distinct. Its Collider stays a full cell
// (see createBlock) — the gap is a drawing detail and must not change what
// collides.
Sprite cellSprite(unsigned char r, unsigned char g, unsigned char b) {
    return Sprite{kCellSize - 2, kCellSize - 2, r, g, b, 255};
}

// The textured look: one tile of the sheet, filling the whole cell. The
// artwork carries its own margin, so it doesn't need the 2px trim.
Sprite tileSprite(int tileIndex) {
    Sprite sprite;
    sprite.width = kCellSize;
    sprite.height = kCellSize;
    sprite.texture = spriteSheet;
    sprite.srcX = tileIndex * kTileSize;
    sprite.srcY = 0;
    sprite.srcW = kTileSize;
    sprite.srcH = kTileSize;
    sprite.layer = kBoardLayer;
    return sprite;
}

// If the sheet failed to load, every one of these quietly returns the flat
// rectangle instead. Delete assets/ and the game still plays — it just looks
// like it did before there was any art.
Sprite headSprite() {
    return spriteSheet ? tileSprite(kHeadTile) : cellSprite(130, 230, 130);
}
Sprite bodySprite() {
    return spriteSheet ? tileSprite(kBodyTile) : cellSprite(70, 170, 90);
}
Sprite foodSprite() {
    return spriteSheet ? tileSprite(kFoodTile) : cellSprite(230, 80, 80);
}

// Creates one rectangle in the world: where it is (Transform), what it looks
// like (Sprite), and what it collides with (Collider). The caller supplies
// the Sprite, so a block can be drawn smaller than it collides — which is
// exactly why Sprite and Collider are separate components.
Entity createBlock(World& world, Cell cell, int cellsWide, int cellsTall,
                   Sprite sprite) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{static_cast<float>(cell.x * kCellSize),
                                         static_cast<float>(cell.y * kCellSize)});
    world.addComponent(entity, sprite);
    world.addComponent(entity, Collider{cellsWide * kCellSize,
                                        cellsTall * kCellSize});
    return entity;
}

// Moves an entity that already exists to a grid cell, by rewriting the only
// thing the renderer actually reads: its Transform.
void placeAt(World& world, Entity entity, Cell cell) {
    if (Transform* transform = world.getComponent<Transform>(entity)) {
        transform->x = static_cast<float>(cell.x * kCellSize);
        transform->y = static_cast<float>(cell.y * kCellSize);
    }
}

// --- Text helpers ----------------------------------------------------------

// A line of text centered horizontally in the window. textWidth() comes from
// the engine's font, which is what makes centering possible without the game
// knowing anything about glyph shapes.
// A translucent panel covering the window, used to dim the board behind an
// overlay's text. It works because the renderer enables alpha blending and
// because its layer puts it above the board — before those two things it
// would have been an opaque rectangle in an unpredictable position.
Entity createBackdrop(World& world) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{0.0f, 0.0f});

    Sprite panel;
    panel.width = kWindowWidth;
    panel.height = kWindowHeight;
    panel.r = 10;
    panel.g = 10;
    panel.b = 18;
    panel.a = 170;  // ~2/3 opaque: the board shows through, dimmed
    panel.layer = kOverlayLayer;
    world.addComponent(entity, panel);

    return entity;
}

Entity createCenteredText(World& world, const std::string& value, int y,
                          int scale, unsigned char r, unsigned char g,
                          unsigned char b) {
    Entity entity = world.createEntity();
    const int x = (kWindowWidth - textWidth(value, scale)) / 2;

    world.addComponent(entity, Transform{static_cast<float>(x),
                                         static_cast<float>(y)});
    world.addComponent(entity, Text{value, scale, r, g, b, 255});
    return entity;
}

// --- Game state ------------------------------------------------------------

// One link in the snake: the entity the engine draws, plus the cell the game
// thinks it occupies. Keeping both in one struct means they can't drift out
// of sync the way two parallel vectors would.
struct Segment {
    Entity entity = kInvalidEntity;
    Cell cell;
};

// All the round's bookkeeping in one place. This is NOT an ECS component —
// components are per-entity data the engine iterates over, while this is the
// game's own state. The functions below follow the same shape the engine uses
// for systems: plain data in a struct, plain functions that operate on it.
struct GameState {
    std::vector<Segment> snake;  // snake[0] is the head, back() is the tail
    Cell direction{1, 0};        // cells per tick; {1,0} is "moving right"
    Cell nextDirection{1, 0};    // buffered input, applied at the next tick
    Entity food = kInvalidEntity;
    bool gameOver = false;
    bool won = false;
    int score = 0;

    TickTimer tick{kTickSeconds};
    std::mt19937 rng{std::random_device{}()};
};

bool cellHasSnake(const GameState& state, Cell cell) {
    for (const Segment& segment : state.snake) {
        if (sameCell(segment.cell, cell)) return true;
    }
    return false;
}

// --- Board setup -----------------------------------------------------------

// The border. Four slabs, not a ring of individual cells: a wall is one
// entity whose Collider spans a whole edge, which is cheaper and reads
// better, and the AABB test doesn't care how big the rectangles are.
Entity createWall(World& world, Cell cell, int cellsWide, int cellsTall) {
    Sprite sprite{cellsWide * kCellSize, cellsTall * kCellSize,
                  60, 60, 80, 255};
    Entity wall = createBlock(world, cell, cellsWide, cellsTall, sprite);
    world.addComponent(wall, Wall{});
    return wall;
}

void createWalls(World& world, std::vector<Entity>& walls) {
    walls.push_back(createWall(world, Cell{0, 0}, kGridCols, 1));
    walls.push_back(createWall(world, Cell{0, kGridRows - 1}, kGridCols, 1));
    walls.push_back(createWall(world, Cell{0, 1}, 1, kGridRows - 2));
    walls.push_back(createWall(world, Cell{kGridCols - 1, 1}, 1,
                               kGridRows - 2));
}

// Puts the food on a random cell that is neither wall nor snake.
//
// The obvious approach — pick a random cell, try again if it's taken — is
// fine while the board is mostly empty, but it gets slower as the snake
// fills the board and never finishes at all once the board is full. Listing
// the free cells first costs one cheap pass and is always correct.
void spawnFood(World& world, GameState& state) {
    std::vector<Cell> freeCells;
    for (int y = 1; y < kGridRows - 1; ++y) {  // 1..rows-2 skips the border
        for (int x = 1; x < kGridCols - 1; ++x) {
            Cell cell{x, y};
            if (!cellHasSnake(state, cell)) freeCells.push_back(cell);
        }
    }

    if (freeCells.empty()) {
        // The snake covers every playable cell: there is nowhere left to put
        // food, which is the only way to actually beat this game.
        state.gameOver = true;
        state.won = true;
        return;
    }

    std::uniform_int_distribution<std::size_t> pick(0, freeCells.size() - 1);
    const Cell cell = freeCells[pick(state.rng)];

    // Reuse the one food entity across respawns; only its Transform changes.
    if (state.food == kInvalidEntity) {
        state.food = createBlock(world, cell, 1, 1, foodSprite());
        world.addComponent(state.food, Food{});
    } else {
        placeAt(world, state.food, cell);
    }
}

// Removes the snake and the food. Destruction is deferred: this can be
// called from inside a scene transition, and queuing means it doesn't matter
// what else is mid-iteration when it happens.
void clearRound(World& world, GameState& state) {
    for (const Segment& segment : state.snake) {
        world.destroyLater(segment.entity);
    }
    state.snake.clear();

    if (state.food != kInvalidEntity) {
        world.destroyLater(state.food);
        state.food = kInvalidEntity;
    }
}

// Clears whatever was left of the last round and builds a fresh snake. The
// walls belong to the scene, not the round, so they are left alone.
void startRound(World& world, GameState& state) {
    clearRound(world, state);

    state.direction = Cell{1, 0};
    state.nextDirection = state.direction;
    state.gameOver = false;
    state.won = false;
    state.score = 0;
    state.tick.reset();

    // Start mid-board facing right, with the tail trailing off to the left.
    const Cell head{kGridCols / 2, kGridRows / 2};
    for (int i = 0; i < kStartLength; ++i) {
        const Cell cell{head.x - i, head.y};
        const bool isHead = (i == 0);

        Entity entity = createBlock(world, cell, 1, 1,
                                    isHead ? headSprite() : bodySprite());
        world.addComponent(entity, SnakeSegment{});
        if (isHead) {
            // The head is the entity the player actually steers, so it gets
            // the engine's PlayerControlled tag. Nothing in the engine reads
            // it — it is here so the intent is recorded in the world itself
            // rather than only in this file.
            world.addComponent(entity, PlayerControlled{});
        }
        state.snake.push_back(Segment{entity, cell});
    }

    spawnFood(world, state);
}

// --- Per-frame and per-tick logic ------------------------------------------

// Runs every frame, so a key press between two ticks is never missed: it is
// remembered in nextDirection and applied when the next tick fires.
void readDirectionInput(GameState& state, InputManager& input) {
    Cell wanted = state.nextDirection;
    if (input.isKeyDown(SDL_SCANCODE_LEFT))  wanted = Cell{-1, 0};
    if (input.isKeyDown(SDL_SCANCODE_RIGHT)) wanted = Cell{1, 0};
    if (input.isKeyDown(SDL_SCANCODE_UP))    wanted = Cell{0, -1};
    if (input.isKeyDown(SDL_SCANCODE_DOWN))  wanted = Cell{0, 1};

    // A snake cannot turn back into its own neck, so reversals are ignored.
    // The comparison is against `direction` (where the snake is actually
    // travelling) and not `nextDirection`: while moving right, pressing Up
    // and then Left inside a single tick would otherwise leave the snake
    // headed left, straight into itself, without ever having moved up.
    const bool reversing = wanted.x == -state.direction.x &&
                           wanted.y == -state.direction.y;
    if (!reversing) state.nextDirection = wanted;
}

// One tick: the snake's entire turn happens here, and nothing happens
// between ticks. This is the heart of the game.
void stepSnake(World& world, GameState& state) {
    state.direction = state.nextDirection;

    // 1. Where is the head going? One cell, in the current direction.
    const Cell headCell = state.snake.front().cell;
    const Cell newHead{headCell.x + state.direction.x,
                       headCell.y + state.direction.y};

    // 2. Shuffle the body up: every segment takes the cell of the one ahead
    //    of it. Walking backwards from the tail means each cell is read
    //    before it gets overwritten. (The snake always has a head, so
    //    size() - 1 is safe.)
    //
    //    The tail's old cell matters twice over: it is where a new segment
    //    goes if we eat this tick, and because the tail vacates it *before*
    //    collisions are checked, the head is allowed to move into it —
    //    chasing your own tail is legal, as in the original game.
    const Cell oldTailCell = state.snake.back().cell;
    for (std::size_t i = state.snake.size() - 1; i > 0; --i) {
        state.snake[i].cell = state.snake[i - 1].cell;
    }
    state.snake.front().cell = newHead;

    // 3. Push the new cells into the ECS. Until this runs the move has only
    //    happened in the game's own bookkeeping; the engine still has the
    //    snake at its old position and would draw it there.
    for (const Segment& segment : state.snake) {
        placeAt(world, segment.entity, segment.cell);
    }

    // 4. Now that the world is up to date, ask the engine what overlaps.
    //    This runs once per tick rather than once per frame: between ticks
    //    nothing has moved, so the answer could not have changed.
    const Entity head = state.snake.front().entity;
    bool ateFood = false;

    for (const CollisionPair& pair : CollisionSystem(world)) {
        // Only collisions involving the head can mean anything in Snake.
        Entity other = kInvalidEntity;
        if (pair.a == head) {
            other = pair.b;
        } else if (pair.b == head) {
            other = pair.a;
        } else {
            continue;
        }

        // The tags decide what the overlap means. This is the collision
        // *response* that the engine deliberately left to game code.
        if (world.hasComponent<Food>(other)) {
            ateFood = true;
        } else if (world.hasComponent<Wall>(other) ||
                   world.hasComponent<SnakeSegment>(other)) {
            state.gameOver = true;
        }
    }

    if (state.gameOver) return;

    if (ateFood) {
        // Grow by one: a new segment appears in the cell the tail just left,
        // so the snake gets longer without any part of it jumping. Because
        // it is appended at the back it becomes the new tail, and the
        // shuffle in step 2 picks it up automatically from next tick on.
        Entity segment = createBlock(world, oldTailCell, 1, 1, bodySprite());
        world.addComponent(segment, SnakeSegment{});
        state.snake.push_back(Segment{segment, oldTailCell});

        ++state.score;
        spawnFood(world, state);
    }
}

// --- Scenes ----------------------------------------------------------------
//
// Each scene owns the entities it creates and destroys them on the way out.
// Because only the top scene updates, none of them needs to know whether the
// others exist — and because every scene's entities stay in the World until
// it exits, whatever is underneath keeps being drawn.

// An overlay shown on top of the finished board. It doesn't clear anything:
// the dead snake stays visible underneath, which is the whole reason this is
// pushed rather than swapped in.
class GameOverScene : public Scene {
public:
    GameOverScene(bool won, int score) : won_(won), score_(score) {}

    void onEnter(World& world) override {
        const std::string headline = won_ ? "YOU WIN" : "GAME OVER";
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, headline, 190, 5,
                                            240, 90, 90));
        owned_.push_back(createCenteredText(
            world, "SCORE: " + std::to_string(score_), 260, 3, 235, 235, 235));
        owned_.push_back(createCenteredText(world, "R TO PLAY AGAIN", 320, 2,
                                            170, 170, 195));
        owned_.push_back(createCenteredText(world, "ESC TO QUIT", 350, 2,
                                            170, 170, 195));
    }

    void onExit(World& world) override {
        for (Entity entity : owned_) world.destroyLater(entity);
        owned_.clear();
    }

    void update(World& /*world*/, InputManager& input, float /*dt*/,
                SceneStack& scenes) override {
        // Popping this overlay uncovers the play scene, whose onResume sees a
        // finished game and starts a fresh round.
        if (input.wasKeyPressed(SDL_SCANCODE_R)) scenes.pop();
    }

private:
    bool won_;
    int score_;
    std::vector<Entity> owned_;
};

// Pushed on top of the running game. The play scene below stops updating but
// keeps every entity it owns, so the board is frozen rather than unloaded.
class PauseScene : public Scene {
public:
    void onEnter(World& world) override {
        owned_.push_back(createBackdrop(world));
        owned_.push_back(createCenteredText(world, "PAUSED", 210, 5,
                                            235, 235, 235));
        owned_.push_back(createCenteredText(world, "P TO RESUME", 275, 2,
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

// The game proper: owns the walls, the score readout, and the current round.
class PlayScene : public Scene {
public:
    void onEnter(World& world) override {
        createWalls(world, walls_);

        // The score sits on the top wall bar. Text draws after every sprite,
        // so it lands on top of the wall rather than behind it.
        scoreText_ = world.createEntity();
        world.addComponent(scoreText_, Transform{10.0f, 6.0f});
        world.addComponent(scoreText_, Text{"SCORE: 0", 3, 200, 200, 215, 255});

        startRound(world, state_);
    }

    void onExit(World& world) override {
        clearRound(world, state_);
        for (Entity wall : walls_) world.destroyLater(wall);
        walls_.clear();
        world.destroyLater(scoreText_);
        scoreText_ = kInvalidEntity;
    }

    void onResume(World& world) override {
        // Two different scenes pop back to here. After the game-over overlay
        // the round is finished and a new one starts; after pause, nothing
        // should change at all.
        if (state_.gameOver) {
            startRound(world, state_);
            overlayShown_ = false;
        }
    }

    void update(World& world, InputManager& input, float dt,
                SceneStack& scenes) override {
        if (state_.gameOver) {
            if (!overlayShown_) {
                scenes.push(std::make_unique<GameOverScene>(state_.won,
                                                            state_.score));
                overlayShown_ = true;
            }
            return;
        }

        if (input.wasKeyPressed(SDL_SCANCODE_P)) {
            scenes.push(std::make_unique<PauseScene>());
            return;
        }

        readDirectionInput(state_, input);

        // The tick loop. On most frames advance() returns 0 and this body
        // never runs — the snake simply stands still, which is the whole
        // difference between this and the dt-scaled MovementSystem that the
        // engine ran a moment ago.
        const int ticks = state_.tick.advance(dt);
        for (int i = 0; i < ticks && !state_.gameOver; ++i) {
            stepSnake(world, state_);
        }

        refreshScore(world);
    }

private:
    // Rebuilding the string every frame would work and would be wasted; the
    // score changes a few times a minute.
    void refreshScore(World& world) {
        if (state_.score == shownScore_) return;
        if (Text* text = world.getComponent<Text>(scoreText_)) {
            text->value = "SCORE: " + std::to_string(state_.score);
        }
        shownScore_ = state_.score;
    }

    GameState state_;
    std::vector<Entity> walls_;
    Entity scoreText_ = kInvalidEntity;
    int shownScore_ = -1;  // never equal to a real score, so the first
                           // update always writes the readout
    bool overlayShown_ = false;
};

// The first scene. Replacing (rather than pushing) the play scene means the
// title screen is gone for good once the game starts.
class TitleScene : public Scene {
public:
    void onEnter(World& world) override {
        texts_.push_back(createCenteredText(world, "SNAKE", 150, 9,
                                            120, 220, 130));
        texts_.push_back(createCenteredText(world, "ARROW KEYS TO STEER", 290,
                                            2, 200, 200, 215));
        texts_.push_back(createCenteredText(world, "P PAUSES", 320, 2,
                                            150, 150, 175));
        texts_.push_back(createCenteredText(world, "SPACE TO START", 380, 3,
                                            235, 235, 235));
        texts_.push_back(createCenteredText(world, "Q TO QUIT", 430, 2,
                                            150, 150, 175));
    }

    void onExit(World& world) override {
        for (Entity text : texts_) world.destroyLater(text);
        texts_.clear();
    }

    void update(World& /*world*/, InputManager& input, float /*dt*/,
                SceneStack& scenes) override {
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            scenes.replace(std::make_unique<PlayScene>());
        } else if (input.wasKeyPressed(SDL_SCANCODE_Q)) {
            // Popping the last scene empties the stack, which the engine
            // treats as "quit" — game code never touches the Engine itself.
            scenes.pop();
        }
    }

private:
    std::vector<Entity> texts_;
};

}  // namespace

int main(int, char**) {
    Engine gameEngine("Tiny Engine - Snake", kWindowWidth, kWindowHeight);
    World world;

    // Load the artwork once, up front, before any scene asks for a sprite.
    // A failure here is not fatal: spriteSheet stays null and every sprite
    // falls back to a flat colored rectangle.
    spriteSheet = gameEngine.textures().load("assets/snake.png");

    SceneStack scenes;
    scenes.push(std::make_unique<TitleScene>());

    gameEngine.run(world, scenes);
    return 0;
}
