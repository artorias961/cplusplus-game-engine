#pragma once
// ---------------------------------------------------------------------------
// Engine.h — owns the window, the renderer, and the game loop.
//
// This is the one class that ties everything else together:
//   - It opens an SDL window and renderer (the "platform" layer).
//   - It owns an InputManager and feeds it SDL events every frame.
//   - It runs the game loop: process input -> update -> render -> repeat.
//   - It runs a built-in render system over every (Transform, Sprite) pair
//     in the World, so games don't have to write rendering code themselves.
//
// Game-specific logic (e.g. "move the player when arrow keys are held")
// is NOT hardcoded here. It's supplied by the caller as a callback passed
// to run(), which keeps the engine reusable across different games.
// ---------------------------------------------------------------------------

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/Audio.h"
#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Input.h"
#include "engine/Resources.h"
#include "engine/Scene.h"

namespace engine {

// The signature of a per-frame game-logic callback. Called once per frame,
// before the built-in renderer draws anything.
using UpdateFn = std::function<void(World& world, InputManager& input, float dt)>;

class Engine {
public:
    Engine(const std::string& title, int width, int height);
    ~Engine();

    // An Engine owns SDL resources (window/renderer handles) that can't be
    // safely copied, so copying is disabled outright.
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Runs the game loop until the window is closed (or Escape is pressed).
    // `onUpdate` is your game's per-frame logic; the engine calls it once a
    // frame with a delta time already computed for you.
    void run(World& world, const UpdateFn& onUpdate);

    // The same loop, driven by a stack of scenes instead of one callback.
    // The engine updates the top scene, then applies any transition it
    // requested, and stops when the stack runs empty. Games with more than
    // one mode (menu, playing, paused) want this one.
    void run(World& world, SceneStack& scenes);

    // Ends the loop after the current frame. Game code using the callback
    // form calls this to quit; scene-driven games can instead pop their last
    // scene, which amounts to the same thing.
    void quit() { running_ = false; }

    // The shared texture cache. It lives here because loading a texture needs
    // the renderer, and the Engine is what owns that. Game code asks it for
    // images: gameEngine.textures().load("assets/tiles.png").
    TextureCache& textures() { return *textures_; }

    // The sound device, owned here for the same reason: it belongs to the
    // platform layer, and it must be closed before SDL shuts down.
    AudioDevice& audio() { return *audio_; }

    // --- Drawing a frame without running the loop --------------------------
    //
    // These two exist because for four games nothing verified the renderer at
    // all. Every other part of the engine had tests; the one part that decides
    // what a player actually sees was checked by looking at it, which meant a
    // sign error in the camera or a layer sorted the wrong way could only be
    // caught by a human noticing.
    //
    // Together they make a frame inspectable: draw a World into the back
    // buffer, then read the pixels back and assert on them. With SDL's `dummy`
    // video driver that needs no window, no GPU and no display, so it runs in
    // CI like anything else.
    //
    // `captureFrame` is also just a screenshot, which is a feature worth
    // having on its own.

    // Draws every visible component of `world` into the back buffer. Does NOT
    // present: the frame stays readable, which is the whole point.
    void drawWorld(World& world);

    // Reads the back buffer back as 32-bit pixels, row by row, in
    // SDL_PIXELFORMAT_ARGB8888. Returns an empty vector if the read fails.
    std::vector<Uint32> captureFrame(int& width, int& height);

    // The colour at one pixel of the last drawn frame, as 0xAARRGGBB. Costs a
    // full read-back, so a test checking many pixels should call
    // captureFrame() once instead.
    Uint32 pixelAt(int x, int y);

private:
    // Sprites, polygons and text share one sorted draw list, so a layer means
    // the same thing to all three.
    enum class DrawKind { SpriteKind, PolygonKind, TextKind };
    struct DrawItem {
        int layer = 0;
        Entity entity = kInvalidEntity;
        DrawKind kind = DrawKind::SpriteKind;
    };

    void processEvents();
    void render(World& world);
    void drawSprite(World& world, Entity entity, const Camera& camera);
    void drawPolygon(World& world, Entity entity, const Camera& camera);
    void drawTextComponent(World& world, Entity entity, const Camera& camera);
    void drawText(const std::string& text, int x, int y, int scale,
                  SDL_Color color);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    // Held by pointer because it needs the renderer, which doesn't exist
    // until partway through the constructor body.
    std::unique_ptr<TextureCache> textures_;
    std::unique_ptr<AudioDevice> audio_;
    InputManager input_;
    bool running_ = true;

    // Whether the renderer we actually got honours vsync. Asked once at
    // construction, because the frame limiter must not sleep on top of it:
    // see the note in run().
    bool vsync_ = false;
    // Set by the scene-driven run(); null means "always simulate".
    std::function<bool()> shouldSimulate_;

    // Kept between frames so the per-frame draw list and the point buffer
    // reuse their capacity instead of reallocating sixty times a second.
    std::vector<DrawItem> drawList_;
    std::vector<SDL_FPoint> polygonPoints_;
    std::vector<SDL_Rect> glyphRects_;  // one string's pixels, drawn in one call
};

}  // namespace engine
