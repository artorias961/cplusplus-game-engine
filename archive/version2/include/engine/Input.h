#pragma once
// ---------------------------------------------------------------------------
// Input.h — keeps track of "which keys are currently held down".
//
// SDL delivers input as a stream of discrete events (key X went down, key Y
// went up). Game code almost never wants to think in terms of that stream —
// it wants to ask "is the right arrow held right now?" every frame.
// InputManager bridges the two: Engine feeds it every SDL event, and game
// code just calls isKeyDown().
// ---------------------------------------------------------------------------

#include <SDL.h>
#include <unordered_set>

namespace engine {

class InputManager {
public:
    // Called by Engine at the start of every frame, before any events are
    // processed. It snapshots "what was held last frame", which is the only
    // thing needed to tell a key being held from a key being pressed.
    void beginFrame() { previousKeys_ = heldKeys_; }

    // Called by Engine once per SDL event. Not meant to be called by game
    // code directly.
    void handleEvent(const SDL_Event& event) {
        if (event.type == SDL_KEYDOWN) {
            heldKeys_.insert(event.key.keysym.scancode);
        } else if (event.type == SDL_KEYUP) {
            heldKeys_.erase(event.key.keysym.scancode);
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

private:
    std::unordered_set<SDL_Scancode> heldKeys_;
    std::unordered_set<SDL_Scancode> previousKeys_;
};

}  // namespace engine
