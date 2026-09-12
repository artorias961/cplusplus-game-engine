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
    // processed. It clears the edges recorded during the previous frame's
    // polling, so "pressed" means "pressed since the last beginFrame".
    void beginFrame() {
        pressedKeys_.clear();
        releasedKeys_.clear();
        pressedButtons_.clear();
        releasedButtons_.clear();
    }

    // Called by Engine once per SDL event. Not meant to be called by game
    // code directly.
    //
    // Both things are recorded: the HELD set, which answers "right now?", and
    // the EDGE sets, which answer "did it happen during this frame?". They are
    // not the same question, and the difference is the whole reason the edges
    // exist as sets of their own — see wasKeyPressed below.
    void handleEvent(const SDL_Event& event) {
        if (event.type == SDL_KEYDOWN) {
            // SDL repeats KEYDOWN while a key is held. A repeat is not a new
            // press, and counting it as one would make every menu key fire
            // over and over after half a second of holding it.
            if (event.key.repeat == 0) {
                pressedKeys_.insert(event.key.keysym.scancode);
            }
            heldKeys_.insert(event.key.keysym.scancode);
        } else if (event.type == SDL_KEYUP) {
            releasedKeys_.insert(event.key.keysym.scancode);
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
            pressedButtons_.insert(event.button.button);
            heldButtons_.insert(event.button.button);
        } else if (event.type == SDL_MOUSEBUTTONUP) {
            mouseX_ = event.button.x;
            mouseY_ = event.button.y;
            releasedButtons_.insert(event.button.button);
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
    // fires six times: pause, unpause, pause, unpause...
    //
    // This used to be answered by comparing the held set against a snapshot of
    // last frame's, which is right whenever a press outlives the frame it
    // began in — and silently WRONG when it does not. A key that goes down and
    // comes back up between two updates leaves the held set exactly as it
    // found it, so the snapshot comparison sees nothing happen and the press
    // is dropped on the floor. That is not a hypothetical: SDL delivers a
    // whole burst of queued events to one poll, and a fast click, a stalled
    // frame, or a low frame rate all put both halves of a press in the same
    // batch. The bug it produced — a purchase or a pause that just does not
    // happen, occasionally, unreproducibly — is the worst kind.
    //
    // Recording the edge as the event arrives cannot miss it, because it does
    // not infer the press from the state afterwards; it watches the press.
    bool wasKeyPressed(SDL_Scancode key) const {
        return pressedKeys_.find(key) != pressedKeys_.end();
    }

    // Came back up during this frame. The keyboard counterpart of
    // wasMouseReleased, and the same reasoning applies.
    bool wasKeyReleased(SDL_Scancode key) const {
        return releasedKeys_.find(key) != releasedKeys_.end();
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
        return pressedButtons_.find(button) != pressedButtons_.end();
    }

    // Came back up on THIS frame — what ends a drag.
    bool wasMouseReleased(Uint8 button = SDL_BUTTON_LEFT) const {
        return releasedButtons_.find(button) != releasedButtons_.end();
    }

private:
    // What is down right now, for the "is it held?" questions.
    std::unordered_set<SDL_Scancode> heldKeys_;
    std::unordered_set<Uint8> heldButtons_;

    // What CHANGED during this frame, for the "did it happen?" questions.
    // Cleared by beginFrame and filled by handleEvent, so a press and its
    // release landing in the same batch of events are both still visible.
    std::unordered_set<SDL_Scancode> pressedKeys_;
    std::unordered_set<SDL_Scancode> releasedKeys_;
    std::unordered_set<Uint8> pressedButtons_;
    std::unordered_set<Uint8> releasedButtons_;

    int mouseX_ = 0;
    int mouseY_ = 0;
};

}  // namespace engine
