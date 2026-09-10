#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

#include "engine/Font.h"
#include "engine/Systems.h"
#include "engine/View.h"

namespace engine {

namespace {
constexpr int kTargetFps = 60;
constexpr float kTargetFrameSeconds = 1.0f / kTargetFps;
// Clamp huge frame times (e.g. the debugger paused the process) so a single
// slow frame can't cause things to teleport across the screen.
constexpr float kMaxFrameSeconds = 0.25f;
// Spelled out rather than using M_PI, which isn't standard C++ and needs
// _USE_MATH_DEFINES before <cmath> on MSVC.
constexpr double kPi = 3.14159265358979323846;
}  // namespace

Engine::Engine(const std::string& title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, width, height,
                                SDL_WINDOW_SHOWN);
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    // Prefer a hardware-accelerated renderer with vsync. Some environments
    // (headless CI, remote desktops, the "dummy" SDL video driver) have no
    // accelerated backend at all, so if that request fails, fall back to
    // whatever renderer SDL can give us (typically software) rather than
    // crashing outright. Real engines need this kind of fallback constantly.
    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::cerr << "Accelerated renderer unavailable (" << SDL_GetError()
                  << "), falling back to the default renderer.\n";
        renderer_ = SDL_CreateRenderer(window_, -1, 0);
    }
    if (!renderer_) {
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }

    // Did we actually get vsync? The request above can be granted, refused, or
    // quietly dropped by the fallback, and the frame limiter's behaviour has to
    // follow what happened rather than what was asked for.
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer_, &info) == 0) {
        vsync_ = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    }

    // Without this, alpha is silently ignored and every draw is opaque —
    // the `a` on Sprite would be a field that does nothing. With it, a
    // half-transparent rectangle over the board dims what's underneath.
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // SDL_image needs to be told which formats to prepare. PNG is the only
    // one this project uses; a failure here isn't fatal, it just means
    // textures won't load and sprites fall back to colored rectangles.
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        std::cerr << "IMG_Init(PNG) failed: " << IMG_GetError() << "\n";
    }

    textures_ = std::make_unique<TextureCache>(renderer_);

    // Audio is opened as a separate SDL subsystem, and its constructor says so
    // and carries on if the machine has no working sound device — silence is a
    // far better failure than refusing to start.
    audio_ = std::make_unique<AudioDevice>();
}

Engine::~Engine() {
    // Order matters. The audio device runs a callback on its own thread, so it
    // is closed first — otherwise that thread can still be mixing while the
    // rest of this is torn down. Then the texture cache, because every texture
    // belongs to the renderer and must be released before it.
    audio_.reset();
    textures_.reset();

    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    IMG_Quit();
    SDL_Quit();
}

void Engine::processEvents() {
    // Snapshot last frame's held keys before anything changes them, so
    // input.wasKeyPressed() can tell a new press from a key still held.
    input_.beginFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running_ = false;
        } else if (event.type == SDL_KEYDOWN &&
                   event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            running_ = false;
        }
        // Every other key event goes to the InputManager so game code can
        // query "is this key held right now?" via input.isKeyDown(...).
        input_.handleEvent(event);
    }
}

void Engine::render(World& world) {
    drawWorld(world);
    SDL_RenderPresent(renderer_);
}

// Everything render() does except presenting. Split out so a test can draw a
// frame and then read it back before it is thrown at the screen — see the note
// on drawWorld/captureFrame in Engine.h.
void Engine::drawWorld(World& world) {
    // Clear to a dark background color.
    SDL_SetRenderDrawColor(renderer_, 24, 24, 32, 255);
    SDL_RenderClear(renderer_);

    // Where the view is. The first Camera in the world wins; with none, the
    // view sits at the origin and world space and screen space coincide.
    Camera camera;
    for (auto& entry : world.view<Camera>()) {
        camera = entry.second;
        break;
    }

    // The built-in render system. This is the only place in the engine that
    // touches SDL's drawing calls directly — everything else works through
    // components.
    //
    // Sprites, polygons and text all go into ONE list, sorted by (layer, id).
    // Component pools iterate in an arbitrary order, which is invisible until
    // two things overlap and the map starts deciding which one wins; the layer
    // is the game's say in that, and the ID tiebreak keeps the order identical
    // from frame to frame so nothing flickers. Sorting the three kinds
    // together is what lets a dimming panel sit above the board and below the
    // menu text, which three separate passes could never express.
    //
    // The list is rebuilt each frame — a small cost this engine can afford. A
    // bigger one would keep it and re-sort only when something is added,
    // removed, or changes layer.
    drawList_.clear();
    for (auto& [entity, sprite] : world.view<Sprite>()) {
        if (!world.hasComponent<Transform>(entity)) continue;  // nowhere to draw
        drawList_.push_back(DrawItem{sprite.layer, entity, DrawKind::SpriteKind});
    }
    for (auto& [entity, polygon] : world.view<Polygon>()) {
        if (!world.hasComponent<Transform>(entity)) continue;
        drawList_.push_back(DrawItem{polygon.layer, entity, DrawKind::PolygonKind});
    }
    for (auto& [entity, text] : world.view<Text>()) {
        if (!world.hasComponent<Transform>(entity)) continue;
        drawList_.push_back(DrawItem{text.layer, entity, DrawKind::TextKind});
    }

    std::sort(drawList_.begin(), drawList_.end(),
              [](const DrawItem& a, const DrawItem& b) {
                  if (a.layer != b.layer) return a.layer < b.layer;
                  if (a.entity != b.entity) return a.entity < b.entity;
                  return a.kind < b.kind;
              });

    for (const DrawItem& item : drawList_) {
        switch (item.kind) {
            case DrawKind::SpriteKind:
                drawSprite(world, item.entity, camera);
                break;
            case DrawKind::PolygonKind:
                drawPolygon(world, item.entity, camera);
                break;
            case DrawKind::TextKind:
                drawTextComponent(world, item.entity, camera);
                break;
        }
    }
}

std::vector<Uint32> Engine::captureFrame(int& width, int& height) {
    width = 0;
    height = 0;
    if (SDL_GetRendererOutputSize(renderer_, &width, &height) != 0) return {};
    if (width <= 0 || height <= 0) return {};

    std::vector<Uint32> pixels(static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height));
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                             pixels.data(),
                             width * static_cast<int>(sizeof(Uint32))) != 0) {
        return {};
    }
    return pixels;
}

Uint32 Engine::pixelAt(int x, int y) {
    int width = 0;
    int height = 0;
    const std::vector<Uint32> pixels = captureFrame(width, height);
    if (pixels.empty()) return 0;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
}

void Engine::drawSprite(World& world, Entity entity, const Camera& camera) {
    const Sprite& sprite = *world.getComponent<Sprite>(entity);
    const Transform& transform = *world.getComponent<Transform>(entity);

    SDL_Rect rect{
        static_cast<int>(viewToScreenX(camera, transform.x, sprite.screenSpace,
                                       sprite.parallax)),
        static_cast<int>(viewToScreenY(camera, transform.y, sprite.screenSpace,
                                       sprite.parallax)),
        sprite.width,
        sprite.height,
    };

    {
        if (sprite.texture) {
            // r/g/b/a act as a tint multiplied into the artwork; all 255s
            // (the default) leave it exactly as painted.
            SDL_SetTextureColorMod(sprite.texture, sprite.r, sprite.g, sprite.b);
            SDL_SetTextureAlphaMod(sprite.texture, sprite.a);

            // A zero-sized source rect means "the whole image"; otherwise it
            // selects one tile out of a sheet.
            SDL_Rect source{sprite.srcX, sprite.srcY, sprite.srcW, sprite.srcH};
            const SDL_Rect* sourcePtr =
                (sprite.srcW > 0 && sprite.srcH > 0) ? &source : nullptr;

            // RenderCopyEx is RenderCopy plus rotation. SDL measures the
            // angle in degrees, while Transform stores radians, so this is
            // the one place in the project that converts between them.
            // Passing a null center rotates about the middle of the
            // destination rectangle, which is what you almost always want.
            const double degrees =
                static_cast<double>(transform.rotation) * 180.0 / kPi;

            // Mirroring is free here and expensive in art: SDL flips the
            // sampling, so a unit facing left costs nothing extra, where
            // drawing every left-facing frame by hand would double the sheet.
            const SDL_RendererFlip flip =
                sprite.flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(renderer_, sprite.texture, sourcePtr, &rect,
                             degrees, nullptr, flip);
        } else {
            // Note that a plain colored rectangle ignores rotation: SDL fills
            // axis-aligned rects only. Rotating untextured shapes is what the
            // Polygon component below is for.
            SDL_SetRenderDrawColor(renderer_, sprite.r, sprite.g, sprite.b,
                                   sprite.a);
            SDL_RenderFillRect(renderer_, &rect);
        }
    }
}

// The vector-graphics path. Each point is rotated around the entity's
// Transform and then moved into place, which is the whole of 2D rotation in
// two lines:
//
//     x' = x cos(a) - y sin(a)
//     y' = x sin(a) + y cos(a)
//
// cos and sin are computed once per entity rather than once per point, because
// they only depend on the angle.
void Engine::drawPolygon(World& world, Entity entity, const Camera& camera) {
    const Polygon& polygon = *world.getComponent<Polygon>(entity);
    const Transform& transform = *world.getComponent<Transform>(entity);
    if (polygon.points.size() < 2) return;

    const float originX =
        viewToScreenX(camera, transform.x, polygon.screenSpace, polygon.parallax);
    const float originY =
        viewToScreenY(camera, transform.y, polygon.screenSpace, polygon.parallax);

    const float cosA = std::cos(transform.rotation);
    const float sinA = std::sin(transform.rotation);

    polygonPoints_.clear();
    polygonPoints_.reserve(polygon.points.size() + 1);
    for (const Vec2& point : polygon.points) {
        polygonPoints_.push_back(SDL_FPoint{
            originX + point.x * cosA - point.y * sinA,
            originY + point.x * sinA + point.y * cosA,
        });
    }
    // A closed shape just repeats its first point, so the last segment joins
    // back around.
    if (polygon.closed) polygonPoints_.push_back(polygonPoints_.front());

    SDL_SetRenderDrawColor(renderer_, polygon.r, polygon.g, polygon.b,
                           polygon.a);
    SDL_RenderDrawLinesF(renderer_, polygonPoints_.data(),
                         static_cast<int>(polygonPoints_.size()));
}

void Engine::drawTextComponent(World& world, Entity entity,
                               const Camera& camera) {
    const Text& text = *world.getComponent<Text>(entity);
    const Transform& transform = *world.getComponent<Transform>(entity);

    drawText(
        text.value,
        static_cast<int>(viewToScreenX(camera, transform.x, text.screenSpace)),
        static_cast<int>(viewToScreenY(camera, transform.y, text.screenSpace)),
        text.scale, SDL_Color{text.r, text.g, text.b, text.a});
}

// Draws a string as filled rectangles, one per lit font pixel — the same call
// used for sprites, so text needs no texture and no font file.
//
// The rects for a whole string are gathered first and handed to SDL in ONE
// call. That was not the original shape: it drew each pixel with its own
// SDL_RenderFillRect, and a comment here said the fix was well trodden and not
// yet worth doing. It became worth doing when the fourth game's HUD reached
// about 187 characters on screen at once — roughly 3,200 draw calls a frame,
// or 190,000 a second, to render a scoreboard.
//
// Batching turns that into one call per string, about 17 of them. The pixels
// drawn are identical; only the number of times SDL is asked to draw them
// changes. The other well-trodden fix — baking the glyphs into a texture atlas
// at startup — would be faster still and is not worth the machinery: this is
// now far below the cost of everything else on screen.
//
// The scratch buffer is a member rather than a local so the allocation happens
// once rather than once per string per frame.
void Engine::drawText(const std::string& text, int x, int y, int scale,
                      SDL_Color color) {
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);

    glyphRects_.clear();
    glyphRects_.reserve(text.size() * kGlyphWidth * kGlyphHeight);

    int cursorX = x;
    for (char character : text) {
        const char* glyph = glyphFor(character);

        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int col = 0; col < kGlyphWidth; ++col) {
                if (!glyphPixel(glyph, col, row)) continue;
                glyphRects_.push_back(SDL_Rect{cursorX + col * scale,
                                               y + row * scale, scale, scale});
            }
        }
        cursorX += (kGlyphWidth + kGlyphSpacing) * scale;
    }

    if (glyphRects_.empty()) return;  // an empty string, or all spaces
    SDL_RenderFillRects(renderer_, glyphRects_.data(),
                        static_cast<int>(glyphRects_.size()));
}

// How many frames to run before quitting on our own, or 0 for "until the
// player closes the window".
//
// This exists so the SHIPPED EXECUTABLES can be smoke-tested. Every game's
// rules are driven headlessly by tests/Harness.h, but the harness deliberately
// reimplements the loop — which means main.cpp, the real Engine::run, window
// creation, asset loading and audio startup were the one stretch of this
// project that nothing ever executed. A game that crashed on its first frame
// would have passed every test in the suite.
//
// Set TINY_ENGINE_MAX_FRAMES=120 and the game plays two seconds and exits 0.
// With SDL_VIDEODRIVER=dummy alongside it, that happens on a machine with no
// display at all, which is what lets CI run it.
namespace {
int maxFramesFromEnvironment() {
    const char* setting = SDL_getenv("TINY_ENGINE_MAX_FRAMES");
    if (!setting) return 0;

    const int frames = SDL_atoi(setting);
    return frames > 0 ? frames : 0;
}
}  // namespace

void Engine::run(World& world, const UpdateFn& onUpdate) {
    Uint64 previousTicks = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();

    const int maxFrames = maxFramesFromEnvironment();
    int framesRun = 0;

    while (running_) {
        if (maxFrames > 0 && framesRun++ >= maxFrames) break;
        // --- 1. Timing: how long did the last frame actually take? ---
        Uint64 currentTicks = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(currentTicks - previousTicks) /
                   static_cast<float>(frequency);
        previousTicks = currentTicks;
        if (dt > kMaxFrameSeconds) dt = kMaxFrameSeconds;

        // --- 2. Input: turn queued OS events into "is this key held?" state.
        processEvents();

        // --- 3. Update: run built-in systems, then the game's own logic.
        // A paused scene can switch the systems off for the frame, so the
        // world genuinely stops instead of drifting on beneath the overlay.
        if (!shouldSimulate_ || shouldSimulate_()) {
            RunBuiltinSystems(world, dt);
        }
        if (onUpdate) onUpdate(world, input_, dt);

        // --- 4. Deletions: entities queued with destroyLater() during the
        // update are erased here, where nothing is iterating a pool.
        world.flushDestroyed();

        // --- 5. Render: draw the current state of the world.
        render(world);

        // --- 6. Frame limiting: if we finished early, sleep the remainder
        // so we don't burn 100% CPU rendering thousands of frames a second.
        //
        // ONLY when vsync isn't doing it for us. This used to sleep either
        // way, and the two limiters fight on any display faster than 60Hz:
        // present returns after one refresh (6.9ms at 144Hz), the sleep adds
        // another 9.7ms on top, and the NEXT present then has to wait for the
        // following refresh boundary — landing at 20.8ms, so a 144Hz monitor
        // runs the game at 48fps with uneven frame times instead of 60.
        //
        // A 60Hz display never showed it: present already costs a full
        // 16.6ms there, so the sleep computes to zero and the bug is exactly
        // invisible on the machine most likely to be testing for it.
        if (!vsync_) {
            Uint64 frameTicks = SDL_GetPerformanceCounter() - currentTicks;
            float frameSeconds =
                static_cast<float>(frameTicks) / static_cast<float>(frequency);
            if (frameSeconds < kTargetFrameSeconds) {
                SDL_Delay(static_cast<Uint32>(
                    (kTargetFrameSeconds - frameSeconds) * 1000.0f));
            }
        }
    }
}

// The scene-driven loop is not a second loop: it's the callback loop above
// with a three-line callback. Keeping one implementation of the game loop
// means timing, input and frame limiting can't drift apart between the two
// entry points — and it makes plain that scenes are built on the public API,
// not wired into the engine's internals.
void Engine::run(World& world, SceneStack& scenes) {
    // Whatever the game pushed before calling run() is still only queued.
    // Apply it now, so the first frame has a scene to update.
    scenes.applyPending(world);

    // Consulted each frame, before the systems run, so a pause takes effect
    // immediately rather than a frame late.
    shouldSimulate_ = [&scenes]() { return scenes.simulating(); };

    run(world, [&scenes, this](World& w, InputManager& input, float dt) {
        scenes.update(w, input, dt);

        // Transitions the scene just requested are applied here, after its
        // update() has returned — never while it is still running.
        scenes.applyPending(w);

        // Popping the last scene means the game is over in the largest sense.
        if (scenes.empty()) quit();
    });

    // That lambda captured `scenes` by reference. Leaving it installed would
    // leave the Engine holding a reference to a stack the caller is free to
    // destroy the moment run() returns — and a later run() with a plain
    // callback would then call it. Clearing it makes "always simulate" the
    // default again, which is what the callback form expects.
    shouldSimulate_ = nullptr;
}

}  // namespace engine
