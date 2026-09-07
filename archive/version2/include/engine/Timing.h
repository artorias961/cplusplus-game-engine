#pragma once
// ---------------------------------------------------------------------------
// Timing.h — turning variable-length frames into fixed-length "ticks".
//
// The game loop hands game code a `dt` that is slightly different every
// frame: 16.1ms, then 17.4ms, then 15.9ms. That is exactly what you want for
// smooth motion — MovementSystem multiplies by dt, so an entity travels the
// same distance per second no matter how the frames fall — and exactly what
// you do NOT want for a game that happens in discrete steps. A grid game
// can't move "0.42 of a cell this frame"; it has to sit still and then jump
// one whole cell, on a schedule of its own.
//
// TickTimer bridges the two. Feed it every frame's dt and it tells you how
// many fixed-length steps have elapsed. It knows nothing about grids or
// games: the interval is whatever the caller passes in.
// ---------------------------------------------------------------------------

namespace engine {

class TickTimer {
public:
    // `intervalSeconds` is how long one tick lasts — 0.12f means the caller
    // gets a step roughly eight times a second.
    explicit TickTimer(float intervalSeconds) : interval_(intervalSeconds) {}

    // Banks one frame's worth of time and returns how many whole ticks fit
    // in the bank — usually 0 (this frame was shorter than a tick) or 1.
    //
    // The leftover stays banked rather than being thrown away, which is the
    // whole point: at 60fps a 0.12s tick doesn't divide evenly into frames,
    // so discarding the remainder would make every tick land a little late
    // and the whole game would slowly drift off tempo.
    //
    // It can return more than 1 when a frame ran long (a stall, dragging the
    // window). Running the caller's step that many times is what keeps game
    // speed independent of frame rate. Engine::run already clamps dt to
    // 0.25s, so a long freeze can't come back as hundreds of queued ticks.
    int advance(float dt) {
        if (interval_ <= 0.0f) return 0;  // A zero interval would never drain.

        accumulated_ += dt;
        int ticks = 0;
        while (accumulated_ >= interval_) {
            accumulated_ -= interval_;
            ++ticks;
        }
        return ticks;
    }

    // Throws away banked time, so the next tick is a full interval away.
    // Useful when restarting: a new round shouldn't inherit a near-full
    // accumulator and immediately step.
    void reset() { accumulated_ = 0.0f; }

private:
    float interval_ = 0.0f;
    float accumulated_ = 0.0f;
};

}  // namespace engine
