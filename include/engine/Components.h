#pragma once
// ---------------------------------------------------------------------------
// Components.h — plain data, no behavior.
//
// This is the core ECS rule: a component is a struct with fields and nothing
// else. No methods that "do" things, no logic. All behavior lives in systems
// (see MovementSystem in Systems.h and the render loop in Engine.cpp) that
// read and write these fields.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

// Declared, not included: SDL_Texture is an opaque type, so a Sprite can hold
// a pointer to one without this header pulling in all of SDL. Components stay
// cheap to include from anywhere.
struct SDL_Texture;

namespace engine {

// A point or direction in 2D. Transform predates this and keeps its own
// x/y fields so existing code and aggregate initialization still work.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// The view onto the world: everything drawn in world space is shifted by the
// negative of this, so moving the camera right slides the world left.
//
// It is a component on an entity rather than a field on the Engine for two
// reasons: game code reaches it through the World it already has (a scene
// never sees the Engine), and a test can move the view without a window
// existing. The renderer uses the first Camera it finds; if there is none the
// view sits at the origin, which is why the games written before this existed
// still draw exactly as they did.
//
// Anything that should ignore the camera — a score, a menu, a full-screen
// dimming panel — sets `screenSpace` on its Sprite, Polygon or Text instead.
//
// Between "moves with the world" and "ignores the camera" there is a third
// case, which is what `parallax` on Sprite and Polygon is for: distant scenery
// that should slide past more slowly than the ground does, or foreground
// detail that should slide past faster. A factor of 1 is the world, 0 is the
// screen, and anything between is depth. `screenSpace` and `parallax` overlap
// arithmetically — `screenSpace` is `parallax = 0` — but they say different
// things: one is "this is not in the world at all", the other is "this is in
// the world, but far away". `screenSpace` wins where both are set.
//
// Text has no parallax. Nothing has wanted a signpost in the middle distance,
// and a HUD is the only text this project has ever drawn.
struct Camera {
    float x = 0.0f;
    float y = 0.0f;
};

// Where an entity is in the world, in pixels, and which way it faces.
//
// `rotation` is in RADIANS, measured from "pointing right" and increasing
// clockwise on screen (screen y grows downward, which flips the usual
// mathematical convention). Radians because that is what std::cos and
// std::sin take, and game code does far more trigonometry than SDL does —
// the renderer converts to degrees at the single point SDL needs them.
struct Transform {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
};

// How fast an entity is moving, in pixels per second. A MovementSystem reads
// this and Transform together each frame to move things.
struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

// How fast an entity is turning, in radians per second — exactly what
// Velocity is to position, for Transform::rotation. MovementSystem applies
// both, so a tumbling object needs no per-frame code of its own.
struct AngularVelocity {
    float radiansPerSecond = 0.0f;
};

// What an entity looks like on screen.
//
// Two ways to be drawn, and the renderer picks between them by looking at
// `texture`:
//
//   - texture == nullptr: a solid rectangle in (r, g, b, a). No assets
//     needed, which is why the engine can run with an empty assets folder.
//   - texture != nullptr: that image, stretched into the same rectangle. If
//     srcW/srcH are non-zero they select one tile out of a larger sheet,
//     which is how one PNG holds a whole character's worth of frames. The
//     r/g/b/a values become a tint multiplied into the artwork, so 255s
//     everywhere means "draw it as painted".
//
// `layer` controls draw order: lower layers are drawn first, so a higher
// layer lands on top. Entities on the same layer are ordered by entity ID —
// arbitrary, but stable from frame to frame, which is what stops sprites
// flickering past each other. All text draws after all sprites regardless.
struct Sprite {
    int width = 32;
    int height = 32;
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;

    SDL_Texture* texture = nullptr;
    int srcX = 0;
    int srcY = 0;
    int srcW = 0;  // 0 means "use the whole texture"
    int srcH = 0;

    // Draw the artwork mirrored left-to-right.
    //
    // This is the cheapest component field in the engine and it halves how much
    // art a game needs. Without it a unit that can face either way needs a
    // second copy of every single frame drawn, mirrored by hand — and the
    // reference game this project is chasing ships 2,161 unit frames, so "twice
    // as many" is not a rounding error.
    //
    // Only the horizontal axis, because that is what facing means in a
    // side-on 2D game. A vertical flip is a real thing SDL can do and nothing
    // here has ever wanted one, so it is not here.
    bool flipX = false;

    int layer = 0;
    bool screenSpace = false;  // ignore the Camera; draw at fixed coordinates
    float parallax = 1.0f;     // how much of the camera's movement applies
};

// An outline drawn as connected line segments — the "vector graphics" look of
// early arcade games, and the natural partner to Transform::rotation.
//
// Points are in LOCAL space: relative to the entity's Transform, which acts
// as the origin the shape rotates around. So a triangle listed around (0,0)
// spins about its own middle, and the same points can be reused by every
// entity of that kind. The renderer does the rotate-then-translate.
//
// Unlike Sprite, a Polygon needs no texture and no assets at all, and it
// scales to any size without blurring, because it is recomputed every frame
// rather than stretched.
struct Polygon {
    std::vector<Vec2> points;
    bool closed = true;  // join the last point back to the first
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;
    int layer = 0;
    bool screenSpace = false;
    float parallax = 1.0f;
};

// The rectangle an entity collides with, in pixels, anchored at its
// Transform (which is the top-left corner, same as Sprite).
//
// Why not just reuse Sprite's width/height? Because what a thing *looks*
// like and what it *hits* are different questions, and games separate them
// constantly: a character's hitbox is usually narrower than its artwork, a
// pickup's grab radius is usually larger, and decorative scenery has a
// Sprite with no Collider at all. Keeping them apart also means an entity
// can collide while being invisible (trigger zones, level boundaries).
struct Collider {
    int width = 32;
    int height = 32;
};

// The circle an entity collides with — the right shape for anything that
// rotates. A box collider is defined by axis-aligned edges, so it is simply
// wrong for a spinning ship: rotate the ship and the box either stops
// covering it or covers empty space. A circle looks the same at every angle,
// which is why arcade games full of rotating things use circles and why this
// is cheaper than the alternative (SAT on rotated polygons).
//
// Anchoring differs from Collider, deliberately: a circle is centered ON the
// Transform, while a box hangs down-right FROM it. Each is the natural
// convention for its shape — a rotating object wants its pivot at its middle,
// while a rectangle is naturally placed by its corner — and the mixed
// circle/box test below accounts for the difference. Worth knowing when you
// put both on the same entity.
struct CircleCollider {
    float radius = 16.0f;
};

// Destroys the entity once the countdown runs out. LifetimeSystem (Systems.h)
// ticks it down each frame; the engine runs that for you.
//
// This exists because "spawn a thing that removes itself shortly after" —
// bullets, explosions, damage numbers, particles — is otherwise a bookkeeping
// list in every game that needs it.
struct Lifetime {
    float secondsRemaining = 1.0f;
};

// Walks a Sprite along a row of a sprite sheet.
//
// A sheet is one image holding every frame of an animation side by side, and
// `Sprite` could already select one of them: `srcX/srcY/srcW/srcH` cut a
// rectangle out of a texture. What was missing was anything to MOVE that
// rectangle over time, so a game had to advance the frame itself, every frame,
// for every animated thing it owned.
//
// This component and its system are the whole of that. It only ever writes
// `Sprite.srcX`, which is the deliberate limit: frames run left to right along
// one row, and WHICH row — walking, attacking, dying — is the game's decision,
// made by setting `Sprite.srcY`. A component that also picked the animation
// would need to know what animations mean, and that is a game's business.
//
// This was slice 5b on the roadmap and it sat deferred for ten slices with the
// note "owed the moment there are sprites", because building a frame animator
// for art that does not exist is exactly the speculative work this project
// keeps declining. It is here now because art is coming.
struct Animation {
    int frameCount = 1;

    // Width of one frame in the sheet. Zero means "use Sprite.srcW", which is
    // the common case: a sheet of equal tiles where the Sprite is already
    // cropped to one of them.
    int frameWidth = 0;

    float secondsPerFrame = 0.1f;

    // A looping animation runs forever; a one-shot stops on its last frame and
    // clears `playing`, so a game can notice a death or an attack has finished
    // by asking rather than by timing it itself.
    bool loop = true;
    bool playing = true;

    // Where it has got to. Written by AnimationSystem; set `frame` to 0 and
    // `elapsed` to 0 to restart one.
    float elapsed = 0.0f;
    int frame = 0;
};

// A line of text drawn at the entity's Transform, in the built-in bitmap
// font from Font.h. `scale` is how many screen pixels one font pixel becomes,
// so scale 2 gives 10x14 characters and scale 3 gives 15x21.
//
// Making text a component rather than an Engine::drawText call the game makes
// each frame keeps rendering entirely data-driven: a heads-up display is just
// an entity, it lives in the World alongside everything else, and updating
// the score is assigning to a string rather than remembering to draw.
struct Text {
    std::string value;
    int scale = 2;
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;

    // Text used to be drawn after every sprite, unconditionally, so a
    // translucent panel could dim the board but never the score sitting on
    // top of it. Now it takes part in the same layer ordering as everything
    // else, and a heads-up display decides for itself whether an overlay
    // covers it.
    int layer = 0;
    bool screenSpace = false;
};

// Marks the one entity the player directly controls. An empty "tag"
// component like this costs nothing and lets systems ask
// "does this entity have PlayerControlled?" instead of hardcoding an ID.
struct PlayerControlled {};

}  // namespace engine
