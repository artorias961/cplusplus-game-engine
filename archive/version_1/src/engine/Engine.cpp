#include "engine/Engine.h"

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

#include "engine/Font.h"
#include "engine/Systems.h"

namespace engine {

namespace {
constexpr int kTargetFps = 60;
constexpr float kTargetFrameSeconds = 1.0f / kTargetFps;
// Clamp huge frame times (e.g. the debugger paused the process) so a single
// slow frame can't cause things to teleport across the screen.
constexpr float kMaxFrameSeconds = 0.25f;
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
}

Engine::~Engine() {
    // Order matters: every texture belongs to the renderer, so the cache has
    // to release them before the renderer is destroyed underneath it.
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
    // Clear to a dark background color.
    SDL_SetRenderDrawColor(renderer_, 24, 24, 32, 255);
    SDL_RenderClear(renderer_);

    // The built-in render system: draw every entity that has both a
    // Transform (where) and a Sprite (what it looks like). This is the only
    // place in the engine that touches SDL's drawing calls directly —
    // everything else works through components.
    //
    // Component pools iterate in an arbitrary order, which is fine until two
    // sprites overlap and it starts deciding which one wins. So the entities
    // are gathered and sorted by (layer, id) first: layer is the game's
    // choice, and the ID tiebreak keeps the order identical from one frame to
    // the next. The vector is rebuilt each frame, which is a small cost this
    // engine can afford; a bigger one would keep it and only re-sort when a
    // sprite is added, removed, or changes layer.
    std::vector<std::pair<int, Entity>> drawOrder;
    for (auto& [entity, sprite] : world.view<Sprite>()) {
        // A Sprite with no Transform has nowhere to draw.
        if (!world.hasComponent<Transform>(entity)) continue;
        drawOrder.emplace_back(sprite.layer, entity);
    }
    std::sort(drawOrder.begin(), drawOrder.end());

    for (const auto& [layer, entity] : drawOrder) {
        (void)layer;
        const Sprite& sprite = *world.getComponent<Sprite>(entity);
        const Transform& transform = *world.getComponent<Transform>(entity);

        SDL_Rect rect{
            static_cast<int>(transform.x),
            static_cast<int>(transform.y),
            sprite.width,
            sprite.height,
        };

        if (sprite.texture) {
            // r/g/b/a act as a tint multiplied into the artwork; all 255s
            // (the default) leave it exactly as painted.
            SDL_SetTextureColorMod(sprite.texture, sprite.r, sprite.g, sprite.b);
            SDL_SetTextureAlphaMod(sprite.texture, sprite.a);

            // A zero-sized source rect means "the whole image"; otherwise it
            // selects one tile out of a sheet.
            if (sprite.srcW > 0 && sprite.srcH > 0) {
                SDL_Rect source{sprite.srcX, sprite.srcY, sprite.srcW,
                                sprite.srcH};
                SDL_RenderCopy(renderer_, sprite.texture, &source, &rect);
            } else {
                SDL_RenderCopy(renderer_, sprite.texture, nullptr, &rect);
            }
        } else {
            SDL_SetRenderDrawColor(renderer_, sprite.r, sprite.g, sprite.b,
                                   sprite.a);
            SDL_RenderFillRect(renderer_, &rect);
        }
    }

    // Text is drawn after every sprite, so a heads-up display or a "PAUSED"
    // overlay always lands on top of the game rather than under it.
    for (auto& [entity, text] : world.view<Text>()) {
        Transform* transform = world.getComponent<Transform>(entity);
        if (!transform) continue;

        drawText(text.value, static_cast<int>(transform->x),
                 static_cast<int>(transform->y), text.scale,
                 SDL_Color{text.r, text.g, text.b, text.a});
    }

    SDL_RenderPresent(renderer_);
}

// Draws a string one font pixel at a time, each as a filled rectangle — the
// same call used for sprites, so text needs no texture and no font file.
//
// This is not how you'd draw a page of text: a 20-character line at scale 3
// is a few hundred draw calls. For a score and a couple of menu labels that
// is irrelevant, and the fix when it stops being irrelevant is well trodden
// (batch the rects into one SDL_RenderFillRects call, or bake the glyphs into
// a texture atlas once at startup).
void Engine::drawText(const std::string& text, int x, int y, int scale,
                      SDL_Color color) {
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);

    int cursorX = x;
    for (char character : text) {
        const char* glyph = glyphFor(character);

        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int col = 0; col < kGlyphWidth; ++col) {
                if (!glyphPixel(glyph, col, row)) continue;

                SDL_Rect pixel{cursorX + col * scale, y + row * scale, scale,
                               scale};
                SDL_RenderFillRect(renderer_, &pixel);
            }
        }
        cursorX += (kGlyphWidth + kGlyphSpacing) * scale;
    }
}

void Engine::run(World& world, const UpdateFn& onUpdate) {
    Uint64 previousTicks = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();

    while (running_) {
        // --- 1. Timing: how long did the last frame actually take? ---
        Uint64 currentTicks = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(currentTicks - previousTicks) /
                   static_cast<float>(frequency);
        previousTicks = currentTicks;
        if (dt > kMaxFrameSeconds) dt = kMaxFrameSeconds;

        // --- 2. Input: turn queued OS events into "is this key held?" state.
        processEvents();

        // --- 3. Update: run built-in systems, then the game's own logic.
        MovementSystem(world, dt);
        if (onUpdate) onUpdate(world, input_, dt);

        // --- 4. Deletions: entities queued with destroyLater() during the
        // update are erased here, where nothing is iterating a pool.
        world.flushDestroyed();

        // --- 5. Render: draw the current state of the world.
        render(world);

        // --- 6. Frame limiting: if we finished early, sleep the remainder
        // so we don't burn 100% CPU rendering thousands of frames a second.
        // (SDL_RENDERER_PRESENTVSYNC above usually does this for us already;
        // this is a fallback for renderers/platforms where vsync isn't honored.)
        Uint64 frameTicks = SDL_GetPerformanceCounter() - currentTicks;
        float frameSeconds = static_cast<float>(frameTicks) / static_cast<float>(frequency);
        if (frameSeconds < kTargetFrameSeconds) {
            SDL_Delay(static_cast<Uint32>((kTargetFrameSeconds - frameSeconds) * 1000.0f));
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

    run(world, [&scenes, this](World& w, InputManager& input, float dt) {
        scenes.update(w, input, dt);

        // Transitions the scene just requested are applied here, after its
        // update() has returned — never while it is still running.
        scenes.applyPending(w);

        // Popping the last scene means the game is over in the largest sense.
        if (scenes.empty()) quit();
    });
}

}  // namespace engine
