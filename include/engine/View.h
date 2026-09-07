#pragma once
// ---------------------------------------------------------------------------
// View.h — the one rule that turns a world position into a screen position.
//
// Until the fourth game, no world was bigger than its window: the Camera
// existed, but Asteroids only ever nudged it a few pixels for screen shake,
// so world and screen coordinates were always within a hair of each other. A
// scrolling battlefield is the first thing that makes them genuinely differ,
// and the first thing that can be visibly wrong if the rule is wrong.
//
// It lives here, as a pure function, rather than inline in the renderer, for
// exactly one reason: the renderer needs a window and a GPU, so nothing in it
// can be tested. This can. Engine::render calls it, so a test that pins the
// rule down is testing the real thing rather than a copy of it.
//
// It is deliberately NOT paired with a screenToWorld(). Nothing needs one
// yet — that arrives with mouse input, and gets written then.
// ---------------------------------------------------------------------------

#include "engine/Components.h"

namespace engine {

// Where `worldX` lands on screen, given where the view is.
//
// `screenSpace` things — a score, a menu, a dimming panel — ignore the camera
// entirely and are drawn at the coordinates they were given.
inline float viewToScreenX(const Camera& camera, float worldX,
                           bool screenSpace) {
    return screenSpace ? worldX : worldX - camera.x;
}

inline float viewToScreenY(const Camera& camera, float worldY,
                           bool screenSpace) {
    return screenSpace ? worldY : worldY - camera.y;
}

}  // namespace engine
