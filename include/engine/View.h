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

// How much of the camera's movement applies to a thing: 1 for the world, 0 for
// the screen, and anything between for scenery at a distance. Over 1 is a
// foreground that slides past faster than the ground, which is what sells the
// depth in the other direction.
inline float scrollFactor(bool screenSpace, float parallax) {
    return screenSpace ? 0.0f : parallax;
}

// Where `worldX` lands on screen, given where the view is.
//
// `screenSpace` things — a score, a menu, a dimming panel — ignore the camera
// entirely and are drawn at the coordinates they were given.
//
// `parallax` is deliberately a fourth parameter with a default rather than a
// replacement for the third, and that is worth explaining because the tidier
// version is a trap. Collapsing the two into one float would leave every
// existing `viewToScreenX(camera, x, false)` compiling perfectly — `false`
// converts to 0.0f — while silently meaning its exact opposite: the old
// `false` meant "apply the whole camera", the new 0.0f means "apply none of
// it". A signature that quietly inverts its callers is worse than one with an
// extra argument.
inline float viewToScreenX(const Camera& camera, float worldX,
                           bool screenSpace, float parallax = 1.0f) {
    return worldX - camera.x * scrollFactor(screenSpace, parallax);
}

inline float viewToScreenY(const Camera& camera, float worldY,
                           bool screenSpace, float parallax = 1.0f) {
    return worldY - camera.y * scrollFactor(screenSpace, parallax);
}

// And back the other way: what world position is under this screen pixel?
//
// This is what a mouse click needs. SDL reports the cursor in window pixels,
// but the unit standing under it is at a world position that differs by
// however far the view has scrolled — up to a screen and a half, in the game
// that motivated this.
//
// Exact inverses of the two above *for the gameplay plane* — parallax 1, the
// depth the units stand at. There is no `screenSpace` parameter because the
// question does not arise: a screen-space element is already in screen
// coordinates, so hit-testing one needs no conversion at all. And no parallax
// parameter because nothing has ever needed to click on scenery; if something
// does, it wants `screenX + camera.x * parallax`.
inline float screenToWorldX(const Camera& camera, float screenX) {
    return screenX + camera.x;
}

inline float screenToWorldY(const Camera& camera, float screenY) {
    return screenY + camera.y;
}

}  // namespace engine
