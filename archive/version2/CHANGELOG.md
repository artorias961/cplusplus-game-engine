# Changelog

## v1.0.0 — the engine, finished

The smallest readable version of the architecture real engines use, complete
and exercised by three games. Not abandoned: finished. See *Status* and *How
this grew* in the README.

### The engine

- **ECS** — an entity is a `uint32_t`; components are plain structs in maps
  keyed by entity; systems are ordinary functions over those maps. Deferred
  destruction (`destroyLater` / `flushDestroyed`) so code can delete an entity
  from inside a loop over the pool it lives in.
- **Game loop** — delta time with a clamp on long frames, built-in systems, a
  deferred-deletion point, rendering, and frame limiting.
- **Rendering** — sprites (flat colour or texture, whole image or one tile of a
  sheet), vector polygons that rotate exactly, and a 5x7 bitmap font compiled
  into the binary. All three share one draw list sorted by `(layer, entity id)`,
  so a translucent panel can sit above the board and below the menu text. Alpha
  blending throughout.
- **Camera** — a component, not an Engine field, so scenes and tests can move
  the view; anything marked `screenSpace` ignores it.
- **Collision** — box and circle shapes through one system, with both a cheap
  overlap test and full contact geometry: a unit normal along the shortest way
  out, plus penetration depth, plus `reflect()`.
- **Scenes** — a stack, so pausing keeps the game beneath it alive. Transitions
  are queued and applied at a safe point. A scene declares whether the world
  simulates beneath it, which is what makes pause actually pause.
- **Timing** — `TickTimer` turns variable frames into fixed-length ticks and
  banks the remainder, so ticks don't drift.
- **Audio** — square, sine and noise voices synthesised in code and mixed by
  addition, with envelopes to prevent clicks. No SDL_mixer, no sound files.
- **Input** — keys held, and keys pressed this frame; the second is what makes
  menus usable.
- **Resources** — a texture cache that loads each image once, resolves paths
  relative to the executable, and degrades to flat colours if a file is missing.

### The games

- **Asteroids** — rotation, thrust as acceleration with drag, circle collision,
  rocks that split three deep, screen wrap with ghosts drawn across the seam,
  explosion debris, screen shake, and sound.
- **Breakout** — contact-normal bouncing, a paddle that steers the ball,
  sub-frame movement so a fast ball can't tunnel through a brick, and a
  minimum-angle rule so rallies can't flatten out.
- **Snake** — the first game, frozen in `archive/version_1/`.

### Proof it works

- Three test suites: engine arithmetic, and both games' rules driven headlessly
  through a shared harness that runs the same systems in the same order as the
  real loop.
- `engine_bench` measures the deliberately naive parts, so "optimise when it's
  slow" can be answered with a number.
- CI builds and tests on Linux and macOS, running every suite twenty times to
  catch flaky tests rather than trusting a single green run.
