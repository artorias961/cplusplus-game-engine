#pragma once
// ---------------------------------------------------------------------------
// Input.h — keeps track of what is currently held down, on the keyboard and
// on the mouse.
//
// SDL delivers input as a stream of discrete events (key X went down, key Y
// went up). Game code almost never wants to think in terms of that stream —
// it wants to ask "is the right arrow held right now?" every frame.
// InputManager bridges the two: Engine feeds it every SDL event, and game
// code just calls isKeyDown().
//
// The mouse arrived with the fourth game's spawn bar, and is deliberately
// shaped exactly like the keyboard: a held query and a pressed-this-frame
// query, with the same "compare against last frame" trick behind both. A
// button on screen needs the edge for the same reason a menu key does — a
// mouse button stays physically down for several frames, so "is it held?"
// would buy ten units per click.
//
// The position is in SCREEN pixels, which is where SDL reports it and where
// screen-space UI lives. Turning that into a world position is `View.h`'s job,
// because it needs the camera and this class knows nothing about one.
// ---------------------------------------------------------------------------

#include <SDL.h>
#include <unordered_set>

namespace engine {

class InputManager {
public:
    // Called by Engine at the start of every frame, before any events are
    // processed. It snapshots "what was held last frame", which is the only
    // thing needed to tell a key being held from a key being pressed.
    void beginFrame() {
        previousKeys_ = heldKeys_;
        previousButtons_ = heldButtons_;
    }

    // Called by Engine once per SDL event. Not meant to be called by game
    // code directly.
    void handleEvent(const SDL_Event& event) {
        if (event.type == SDL_KEYDOWN) {
            heldKeys_.insert(event.key.keysym.scancode);
        } else if (event.type == SDL_KEYUP) {
            heldKeys_.erase(event.key.keysym.scancode);
        } else if (event.type == SDL_MOUSEMOTION) {
            mouseX_ = event.motion.x;
            mouseY_ = event.motion.y;
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            // A button event carries a position too, and it is the one that
            // matters: a fast click can arrive before any motion event, and
            // trusting only motion would place the click wherever the cursor
            // was last seen.
            mouseX_ = event.button.x;
            mouseY_ = event.button.y;
            heldButtons_.insert(event.button.button);
        } else if (event.type == SDL_MOUSEBUTTONUP) {
            mouseX_ = event.button.x;
            mouseY_ = event.button.y;
            heldButtons_.erase(event.button.button);
        }
    }

    // "Is this key held down right now?" — the right question for continuous
    // actions: steering, walking, holding a trigger.
    bool isKeyDown(SDL_Scancode key) const {
        return heldKeys_.find(key) != heldKeys_.end();
    }

    // "Did this key go down on THIS frame?" — the right question for one-shot
    // actions: pausing, confirming a menu, jumping.
    //
    // Without this, a menu is unusable. A key stays physically down for a
    // tenth of a second or more, which is six-plus frames, so "is P held?"
    // fires six times: pause, unpause, pause, unpause... Comparing against
    // last frame's snapshot turns a held key into a single event.
    bool wasKeyPressed(SDL_Scancode key) const {
        return heldKeys_.find(key) != heldKeys_.end() &&
               previousKeys_.find(key) == previousKeys_.end();
    }

    // --- Mouse -------------------------------------------------------------
    //
    // Position is in screen pixels, with the origin at the top-left of the
    // window — the same space `screenSpace` components are drawn in, so a UI
    // element can be hit-tested against these directly.

    int mouseX() const { return mouseX_; }
    int mouseY() const { return mouseY_; }

    // `button` is an SDL constant: SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT,
    // SDL_BUTTON_MIDDLE.
    bool isMouseDown(Uint8 button = SDL_BUTTON_LEFT) const {
        return heldButtons_.find(button) != heldButtons_.end();
    }

    // Went down on THIS frame. The right question for pressing a button on
    // screen: without it, one physical click spends money every frame it is
    // held.
    bool wasMousePressed(Uint8 button = SDL_BUTTON_LEFT) const {
        return heldButtons_.find(button) != heldButtons_.end() &&
               previousButtons_.find(button) == previousButtons_.end();
    }

    // Came back up on THIS frame — what ends a drag.
    bool wasMouseReleased(Uint8 button = SDL_BUTTON_LEFT) const {
        return heldButtons_.find(button) == heldButtons_.end() &&
               previousButtons_.find(button) != previousButtons_.end();
    }

private:
    std::unordered_set<SDL_Scancode> heldKeys_;
    std::unordered_set<SDL_Scancode> previousKeys_;

    std::unordered_set<Uint8> heldButtons_;
    std::unordered_set<Uint8> previousButtons_;
    int mouseX_ = 0;
    int mouseY_ = 0;
};

}  // namespace engine
