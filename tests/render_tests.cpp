// Before ANY include: SDL.h renames main() unless told otherwise.
#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// render_tests.cpp — asserts about what actually reaches the screen.
//
// For four games this was the hole in the middle of the test suite. Collision
// maths, scene transitions, entity lifetime and every game's rules were all
// checked automatically; the renderer — the one part that decides what a
// player SEES — was checked by looking at it. A sign error in the camera, a
// layer sorted the wrong way, a parallax factor applied to the wrong axis:
// each would have shipped until a human noticed.
//
// The trick that closes it is SDL's `dummy` video driver plus a software
// renderer. Between them there is no window, no GPU and no display, but there
// IS a back buffer, and SDL_RenderReadPixels can read it. So a frame becomes a
// grid of numbers that can be asserted on like anything else, and it runs on a
// CI machine with no graphics hardware at all.
//
// What these tests deliberately do NOT do is compare whole images against
// stored screenshots. Golden images fail for reasons that are not bugs — a
// different SDL version rounding a rectangle differently, a platform blending
// in another order — and a test that cries wolf gets deleted. Each test here
// asserts one specific fact about one specific pixel, chosen so that the only
// thing which can move it is the rule being tested.
// ---------------------------------------------------------------------------

#include <SDL.h>
#include <SDL_image.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "engine/Systems.h"

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

constexpr int kWidth = 320;
constexpr int kHeight = 240;

// The colour Engine clears to. Anything still this colour was not drawn on.
constexpr Uint32 kBackground = 0xFF181820u;

Uint32 rgb(unsigned char r, unsigned char g, unsigned char b) {
    return 0xFF000000u | (static_cast<Uint32>(r) << 16) |
           (static_cast<Uint32>(g) << 8) | static_cast<Uint32>(b);
}

// One frame, as a grid of numbers.
struct Frame {
    std::vector<Uint32> pixels;
    int width = 0;
    int height = 0;

    Uint32 at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return pixels[static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
    bool painted(int x, int y) const { return at(x, y) != kBackground; }

    int paintedCount() const {
        int count = 0;
        for (Uint32 pixel : pixels) {
            if (pixel != kBackground) ++count;
        }
        return count;
    }
};

Frame drawOnce(Engine& engine, World& world) {
    engine.drawWorld(world);
    Frame frame;
    frame.pixels = engine.captureFrame(frame.width, frame.height);
    return frame;
}

Entity addSprite(World& world, float x, float y, int w, int h, unsigned char r,
                 unsigned char g, unsigned char b) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{x, y, 0.0f});

    Sprite sprite;
    sprite.width = w;
    sprite.height = h;
    sprite.r = r;
    sprite.g = g;
    sprite.b = b;
    world.addComponent(entity, sprite);
    return entity;
}

void addCamera(World& world, float x, float y) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Camera{x, y});
}

// The first lit column on a glyph's top row, so a test can point at a pixel
// the font definitely paints rather than guessing one.
int firstLitColumn(char character) {
    const char* glyph = glyphFor(character);
    for (int col = 0; col < kGlyphWidth; ++col) {
        if (glyphPixel(glyph, col, 0)) return col;
    }
    return -1;
}

// --- Textures, for the two tests that need real artwork ---------------------
//
// Everything else in this file draws untextured shapes, which is most of what
// the renderer does. Flipping and frame animation are the two things that only
// mean anything with an image, so these write one.
//
// Written to a PNG and loaded back through `TextureCache` rather than handed
// to SDL directly, because that is the path a game uses: resolve against the
// executable, decode, cache. A test that builds a texture some other way would
// prove the flip works and say nothing about whether a loaded sheet does.

// A two-frame sheet, 8 pixels per frame and 8 tall.
//
// Frame 0 is red on its LEFT half and black on its right; frame 1 is green on
// its left. Both halves matter: the asymmetry is what makes a mirror visible
// (a symmetric image flips to itself and proves nothing), and the two colours
// are what make a frame change visible.
std::string writeTestSheet() {
    SDL_Surface* surface =
        SDL_CreateRGBSurfaceWithFormat(0, 16, 8, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return {};

    auto put = [&](int x, int y, Uint32 colour) {
        static_cast<Uint32*>(surface->pixels)[y * (surface->pitch / 4) + x] =
            colour;
    };
    const Uint32 red = SDL_MapRGBA(surface->format, 220, 40, 40, 255);
    const Uint32 green = SDL_MapRGBA(surface->format, 40, 220, 40, 255);
    const Uint32 black = SDL_MapRGBA(surface->format, 0, 0, 0, 255);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool leftHalfOfItsFrame = (x % 8) < 4;
            const bool secondFrame = x >= 8;
            put(x, y, leftHalfOfItsFrame ? (secondFrame ? green : red) : black);
        }
    }

    // Written to an absolute path, but the RELATIVE name is handed back —
    // `TextureCache::load` resolves against the executable itself, so giving
    // it an already-absolute path prefixes the base directory twice and finds
    // nothing. Which is exactly what happened, and is the reason to load
    // through the cache rather than around it: the test met the same rule a
    // game would.
    const char* name = "render_test_sheet.png";

    std::string path = name;
    if (char* base = SDL_GetBasePath()) {
        path = std::string(base) + name;
        SDL_free(base);
    }
    const bool saved = IMG_SavePNG(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    return saved ? std::string(name) : std::string{};
}

// A sprite showing one 8x8 frame of that sheet, drawn at twice its size so a
// half is four screen pixels wide and easy to point at.
Entity addSheetSprite(World& world, Engine& engine, const std::string& path,
                      float x, float y) {
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{x, y, 0.0f});

    Sprite sprite;
    sprite.texture = engine.textures().load(path);
    sprite.width = 16;
    sprite.height = 16;
    sprite.srcX = 0;
    sprite.srcY = 0;
    sprite.srcW = 8;
    sprite.srcH = 8;
    world.addComponent(entity, sprite);
    return entity;
}

// --- The tests -------------------------------------------------------------

void testSomethingIsDrawnAtAll(Engine& engine) {
    World world;
    addSprite(world, 50.0f, 60.0f, 20, 10, 200, 40, 40);

    const Frame frame = drawOnce(engine, world);
    check(!frame.pixels.empty(), "a frame can be read back at all");
    check(frame.width == kWidth && frame.height == kHeight,
          "and it is the size of the window");
    check(frame.at(0, 0) == kBackground, "an untouched pixel is the clear colour");
    check(frame.at(55, 63) == rgb(200, 40, 40),
          "a sprite paints its own colour where it was put");
    check(frame.at(49, 63) == kBackground,
          "and nothing just outside its left edge");
    check(frame.at(70, 63) == kBackground, "or just past its right edge");
    check(frame.at(55, 59) == kBackground, "or just above it");
    check(frame.at(55, 70) == kBackground, "or just below it");
}

// The rule View.h exists to state, checked against actual pixels rather than
// against itself.
void testTheCameraMovesTheWorldButNotTheHud(Engine& engine) {
    World world;
    addCamera(world, 40.0f, 15.0f);
    addSprite(world, 100.0f, 100.0f, 10, 10, 10, 220, 10);

    Entity hud = addSprite(world, 100.0f, 100.0f, 10, 10, 220, 220, 10);
    world.getComponent<Sprite>(hud)->screenSpace = true;
    world.getComponent<Sprite>(hud)->layer = 1;

    const Frame frame = drawOnce(engine, world);

    check(frame.at(65, 90) == rgb(10, 220, 10),
          "a world sprite is drawn shifted by the negative of the camera");
    check(frame.at(105, 105) == rgb(220, 220, 10),
          "a screen-space sprite ignores the camera entirely");
    check(frame.at(105, 90) == kBackground,
          "and the world sprite is NOT where its coordinates say");
}

// Parallax was the slice-6 engine feature, and the first thing between "moves
// with the world" and "ignores the camera". Half the camera means half the
// shift, and that is now checked in pixels.
void testParallaxMovesThingsAtItsOwnRate(Engine& engine) {
    World world;
    addCamera(world, 80.0f, 0.0f);

    Entity nearby = addSprite(world, 200.0f, 40.0f, 8, 8, 250, 10, 10);
    Entity distant = addSprite(world, 200.0f, 80.0f, 8, 8, 10, 10, 250);
    Entity foreground = addSprite(world, 200.0f, 120.0f, 8, 8, 10, 250, 250);
    world.getComponent<Sprite>(nearby)->parallax = 1.0f;
    world.getComponent<Sprite>(distant)->parallax = 0.5f;
    world.getComponent<Sprite>(foreground)->parallax = 1.5f;

    const Frame frame = drawOnce(engine, world);

    check(frame.at(124, 44) == rgb(250, 10, 10),
          "parallax 1 shifts by the whole camera");
    check(frame.at(164, 84) == rgb(10, 10, 250),
          "parallax 0.5 shifts by half of it");
    check(frame.at(84, 124) == rgb(10, 250, 250),
          "and parallax 1.5 shifts by half again, sliding past faster");
}

// Layers decide what covers what. Getting this backwards would put a HUD
// behind the battlefield, which no headless test could ever have noticed.
void testLayersDecideWhatCoversWhat(Engine& engine) {
    World world;
    Entity under = addSprite(world, 40.0f, 40.0f, 40, 40, 250, 0, 0);
    Entity over = addSprite(world, 50.0f, 50.0f, 40, 40, 0, 0, 250);
    world.getComponent<Sprite>(under)->layer = 5;
    world.getComponent<Sprite>(over)->layer = 6;

    Frame frame = drawOnce(engine, world);
    check(frame.at(60, 60) == rgb(0, 0, 250),
          "where two sprites overlap, the higher layer wins");
    check(frame.at(45, 45) == rgb(250, 0, 0),
          "and the lower one still shows where it is not covered");

    // Swap the layers and the answer must swap with them.
    world.getComponent<Sprite>(under)->layer = 7;
    frame = drawOnce(engine, world);
    check(frame.at(60, 60) == rgb(250, 0, 0),
          "swapping the layers swaps which one is on top");
}

// Alpha is a field on Sprite that does nothing unless the renderer's blend
// mode was set up. It was, four games ago, and nothing has checked it since.
void testAlphaBlends(Engine& engine) {
    World world;
    addSprite(world, 20.0f, 20.0f, 60, 60, 0, 0, 0);

    Entity veil = addSprite(world, 20.0f, 20.0f, 60, 60, 255, 255, 255);
    world.getComponent<Sprite>(veil)->a = 128;
    world.getComponent<Sprite>(veil)->layer = 1;

    const Frame frame = drawOnce(engine, world);
    const int red = static_cast<int>((frame.at(50, 50) >> 16) & 0xFFu);

    check(red > 100 && red < 160,
          "a half-transparent white over black lands about halfway between");
}

// --- Text ------------------------------------------------------------------
//
// The audit batched the font's draw calls: it used to issue one
// SDL_RenderFillRect per lit pixel and now gathers a whole string into one
// SDL_RenderFillRects. That was a 200x reduction in calls, claimed to draw
// exactly the same pixels — and until now nothing could check the claim.

void testTextDrawsTheGlyphItShould(Engine& engine) {
    World world;
    Entity label = world.createEntity();
    world.addComponent(label, Transform{10.0f, 10.0f, 0.0f});

    Text text{"I", 1, 255, 255, 255, 255};
    text.screenSpace = true;
    world.addComponent(label, text);

    const Frame frame = drawOnce(engine, world);

    // Compared against the font table itself: every lit pixel of the glyph
    // must be painted and every unlit one must not. This is what proves the
    // batched path draws the same picture the one-rect-at-a-time path did.
    const char* glyph = glyphFor('I');
    int lit = 0;
    bool matched = true;
    for (int row = 0; row < kGlyphHeight; ++row) {
        for (int col = 0; col < kGlyphWidth; ++col) {
            const bool shouldBeLit = glyphPixel(glyph, col, row);
            if (shouldBeLit) ++lit;
            if (frame.painted(10 + col, 10 + row) != shouldBeLit) matched = false;
        }
    }

    check(lit > 0, "the glyph has pixels to draw");
    check(matched, "every pixel of a drawn glyph matches the font table exactly");
}

void testTextScalesAndAdvances(Engine& engine) {
    World world;
    Entity label = world.createEntity();
    world.addComponent(label, Transform{4.0f, 4.0f, 0.0f});

    Text text{"II", 3, 255, 255, 255, 255};
    text.screenSpace = true;
    world.addComponent(label, text);

    const Frame frame = drawOnce(engine, world);

    const int litColumn = firstLitColumn('I');
    check(litColumn >= 0, "the glyph has a lit pixel on its top row");

    const int x = 4 + litColumn * 3;
    check(frame.painted(x, 4) && frame.painted(x + 2, 4) && frame.painted(x, 6),
          "one font pixel at scale 3 fills a 3x3 block");

    // Measured as a WIDTH rather than by probing one pixel.
    //
    // The first version of this check looked at the first lit column, which
    // for this glyph is column zero — where `col * scale` and `col` are both
    // nought. Dropping the scale from the horizontal offset therefore changed
    // nothing it looked at, and the test passed on a renderer that squashed
    // every string to a third of its width. Spacing is only visible in the
    // distance between columns, so that is what this measures.
    const int advanceWidth = (kGlyphWidth + kGlyphSpacing) * 3;

    // Scanned only as far as the SECOND character starts. Sweeping wider swept
    // that one up too and measured both glyphs as one, which failed against a
    // perfectly correct renderer.
    int leftmost = frame.width;
    int rightmost = -1;
    for (int probe = 0; probe < 4 + advanceWidth; ++probe) {
        if (!frame.painted(probe, 4)) continue;
        if (probe < leftmost) leftmost = probe;
        rightmost = probe;
    }

    const char* glyph = glyphFor('I');
    int lastLit = -1;
    for (int col = 0; col < kGlyphWidth; ++col) {
        if (glyphPixel(glyph, col, 0)) lastLit = col;
    }
    const int expectedWidth = (lastLit - litColumn + 1) * 3;

    check(rightmost >= leftmost, "the top row of the glyph painted something");
    check(rightmost - leftmost + 1 == expectedWidth,
          "and its columns are spaced by the scale, not crammed together");

    check(frame.painted(x + advanceWidth, 4),
          "the next character is one advance further along");
}

void testTextIsCameraAwareLikeEverythingElse(Engine& engine) {
    World world;
    addCamera(world, 30.0f, 0.0f);

    Entity worldLabel = world.createEntity();
    world.addComponent(worldLabel, Transform{100.0f, 50.0f, 0.0f});
    world.addComponent(worldLabel, Text{"I", 1, 255, 255, 255, 255});

    const Frame frame = drawOnce(engine, world);
    const int litColumn = firstLitColumn('I');

    check(frame.painted(70 + litColumn, 50),
          "world-space text moves with the camera");
    check(!frame.painted(100 + litColumn, 50),
          "and is not left behind at its world coordinates");
}

void testAnEmptyWorldDrawsNothing(Engine& engine) {
    World world;
    const Frame frame = drawOnce(engine, world);
    check(frame.paintedCount() == 0,
          "an empty world leaves every pixel at the clear colour");
}

// A Transform with no drawable, and drawables with no Transform: all must be
// skipped rather than crash. The renderer dereferences those components
// without checking, so the guard that filters them out is load-bearing.
void testIncompleteEntitiesAreSkipped(Engine& engine) {
    World world;

    Entity noSprite = world.createEntity();
    world.addComponent(noSprite, Transform{10.0f, 10.0f, 0.0f});

    Entity noTransform = world.createEntity();
    Sprite orphan;
    orphan.width = 50;
    orphan.height = 50;
    world.addComponent(noTransform, orphan);

    Entity textNoTransform = world.createEntity();
    world.addComponent(textNoTransform, Text{"HELLO", 2, 255, 255, 255, 255});

    Entity polygonNoTransform = world.createEntity();
    Polygon shape;
    shape.points = {Vec2{0.0f, 0.0f}, Vec2{10.0f, 10.0f}};
    world.addComponent(polygonNoTransform, shape);

    const Frame frame = drawOnce(engine, world);
    check(frame.paintedCount() == 0,
          "components without a Transform are skipped, not drawn or crashed on");
}

// --- Slice 5b: flipping and frame animation, checked in pixels -------------

void testFlipXMirrorsTheArtwork(Engine& engine) {
    const std::string sheet = writeTestSheet();
    check(!sheet.empty(), "a test sprite sheet can be written");
    if (sheet.empty()) return;

    // Unflipped: the frame is red on its left half, black on its right. Drawn
    // 16 wide at (40,40), so x=42 is inside the left half and x=53 the right.
    {
        World world;
        const Entity entity = addSheetSprite(world, engine, sheet, 40.0f, 40.0f);
        check(world.getComponent<Sprite>(entity)->texture != nullptr,
              "and loaded back through the texture cache");

        const Frame frame = drawOnce(engine, world);
        check(frame.at(42, 44) == rgb(220, 40, 40),
              "an unflipped sprite paints its left half on the left");
        check(frame.at(53, 44) == rgb(0, 0, 0),
              "and its right half on the right");
    }

    // Flipped: the same two probes should report the opposite colours. This is
    // the whole of `Sprite.flipX`, and it is worth a pixel test rather than a
    // field test because the field existing proves nothing — the renderer
    // hardcoded SDL_FLIP_NONE for four games and would have gone on ignoring
    // it silently.
    {
        World world;
        const Entity entity = addSheetSprite(world, engine, sheet, 40.0f, 40.0f);
        world.getComponent<Sprite>(entity)->flipX = true;

        const Frame frame = drawOnce(engine, world);
        check(frame.at(53, 44) == rgb(220, 40, 40),
              "flipX moves the left half of the artwork to the right");
        check(frame.at(42, 44) == rgb(0, 0, 0),
              "and the right half to the left");
    }
}

void testAnimationChangesWhatIsActuallyDrawn(Engine& engine) {
    const std::string sheet = writeTestSheet();
    if (sheet.empty()) return;

    World world;
    const Entity entity = addSheetSprite(world, engine, sheet, 40.0f, 40.0f);

    Animation animation;
    animation.frameCount = 2;
    animation.secondsPerFrame = 0.1f;
    world.addComponent(entity, animation);

    const Frame first = drawOnce(engine, world);
    check(first.at(42, 44) == rgb(220, 40, 40), "frame 0 is the red one");

    // Through RunBuiltinSystems, so this also proves a game gets animation
    // without asking for it.
    RunBuiltinSystems(world, 0.1f);

    const Frame second = drawOnce(engine, world);
    check(second.at(42, 44) == rgb(40, 220, 40),
          "advancing a frame changes the pixels that reach the screen");

    // The link this is really testing is Animation -> Sprite.srcX -> the source
    // rectangle SDL samples. Every step of that could be right in isolation
    // and still not connect, which is how the parallax factor was nearly
    // applied to the wrong axis.
    check(world.getComponent<Sprite>(entity)->srcX == 8,
          "because the source rectangle moved along the sheet");
}

void testPolygonsDraw(Engine& engine) {
    World world;
    Entity entity = world.createEntity();
    world.addComponent(entity, Transform{100.0f, 100.0f, 0.0f});

    Polygon shape;
    shape.points = {Vec2{-20.0f, 0.0f}, Vec2{20.0f, 0.0f}};
    shape.closed = false;
    shape.r = 250;
    shape.g = 200;
    shape.b = 50;
    world.addComponent(entity, shape);

    const Frame frame = drawOnce(engine, world);
    check(frame.at(100, 100) == rgb(250, 200, 50),
          "a polygon draws a line through its Transform");
    check(frame.at(85, 100) == rgb(250, 200, 50), "along its whole length");
    check(frame.at(125, 100) == kBackground, "and stops at its last point");
}

}  // namespace

int main() {
    std::printf("render tests\n");

    // No window, no GPU, no display. `dummy` gives SDL a video driver that
    // does nothing, and the software renderer draws into plain memory — which
    // is exactly what makes the result readable, and what lets this run on a
    // CI machine with no graphics hardware at all.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    std::unique_ptr<Engine> engine;
    try {
        engine = std::make_unique<Engine>("render tests", kWidth, kHeight);
    } catch (const std::exception& error) {
        // A skip, said out loud. Silently passing when the renderer could not
        // be created would turn every check below into a false green.
        std::printf("  could not create a headless renderer: %s\n", error.what());
        std::printf("  SKIPPED\n");
        return 0;
    }

    World empty;
    engine->drawWorld(empty);
    int probeWidth = 0;
    int probeHeight = 0;
    if (engine->captureFrame(probeWidth, probeHeight).empty()) {
        std::printf("  this renderer cannot read pixels back\n");
        std::printf("  SKIPPED\n");
        return 0;
    }

    testSomethingIsDrawnAtAll(*engine);
    testTheCameraMovesTheWorldButNotTheHud(*engine);
    testParallaxMovesThingsAtItsOwnRate(*engine);
    testLayersDecideWhatCoversWhat(*engine);
    testAlphaBlends(*engine);
    testTextDrawsTheGlyphItShould(*engine);
    testTextScalesAndAdvances(*engine);
    testTextIsCameraAwareLikeEverythingElse(*engine);
    testAnEmptyWorldDrawsNothing(*engine);
    testIncompleteEntitiesAreSkipped(*engine);
    testPolygonsDraw(*engine);

    testFlipXMirrorsTheArtwork(*engine);
    testAnimationChangesWhatIsActuallyDrawn(*engine);

    if (failures == 0) {
        std::printf("all %d checks passed\n", checks);
        return 0;
    }
    std::printf("%d of %d checks FAILED\n", failures, checks);
    return 1;
}
