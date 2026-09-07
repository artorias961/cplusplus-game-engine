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
// The inverse below arrived with mouse input, exactly as predicted: a click is
// reported in screen pixels, and the thing it lands on is somewhere else
// entirely once the view has scrolled.
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

// And back the other way: what world position is under this screen pixel?
//
// This is what a mouse click needs. SDL reports the cursor in window pixels,
// but the unit standing under it is at a world position that differs by
// however far the view has scrolled — up to a screen and a half, in the game
// that motivated this.
//
// Exact inverses of the two above, so `screenToWorldX(c, viewToScreenX(c, x))`
// is x for any camera. There is no `screenSpace` parameter because the
// question does not arise: a screen-space element is already in screen
// coordinates, so hit-testing one needs no conversion at all.
inline float screenToWorldX(const Camera& camera, float screenX) {
    return screenX + camera.x;
}

inline float screenToWorldY(const Camera& camera, float screenY) {
    return screenY + camera.y;
}

}  // namespace engine
