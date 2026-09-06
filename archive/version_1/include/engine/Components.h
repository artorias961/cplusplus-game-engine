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

// Declared, not included: SDL_Texture is an opaque type, so a Sprite can hold
// a pointer to one without this header pulling in all of SDL. Components stay
// cheap to include from anywhere.
struct SDL_Texture;

namespace engine {

// Where an entity is in the world, in pixels.
struct Transform {
    float x = 0.0f;
    float y = 0.0f;
};

// How fast an entity is moving, in pixels per second. A MovementSystem reads
// this and Transform together each frame to move things.
struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
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

    int layer = 0;
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
};

// Marks the one entity the player directly controls. An empty "tag"
// component like this costs nothing and lets systems ask
// "does this entity have PlayerControlled?" instead of hardcoding an ID.
struct PlayerControlled {};

}  // namespace engine
