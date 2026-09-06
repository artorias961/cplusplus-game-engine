# Tiny Engine

A minimal 2D game engine in C++17 + SDL2, built to be read end to end in one
sitting. It's the smallest version of the architecture real engines use: a
game loop, an Entity-Component-System, a layered renderer with textures and
bitmap text, input handling, AABB collision, fixed-tick timing and a scene
stack — each in its own file with heavy comments explaining *why*, not just
*what*.

It is **not** trying to be fast, complete, or production-ready. Storage uses
`std::unordered_map` instead of packed arrays, there's no batching, no scene
graph, no audio, and the asset handling is one cache that loads PNGs. Once
this makes sense, those are the natural next things to add — see "Where to go
from here" below.

## What's in the box

```
engine_project/
├── CMakeLists.txt
├── include/engine/
│   ├── ECS.h           Entity type + component storage + World
│   ├── Components.h    Transform, Velocity, Sprite, Collider, Text, ...
│   ├── Systems.h       MovementSystem + CollisionSystem (AABB overlap)
│   ├── Timing.h        TickTimer (variable frames -> fixed-length ticks)
│   ├── Scene.h         Scene + SceneStack (menu / playing / paused / ...)
│   ├── Font.h          A 5x7 bitmap font, built into the binary
│   ├── Resources.h     TextureCache (load each image once, own it)
│   ├── Input.h         InputManager (keys held, and keys just pressed)
│   └── Engine.h        Window/renderer/game-loop owner
├── src/engine/
│   └── Engine.cpp      SDL setup, the loop, and the built-in render system
├── src/game/
│   └── main.cpp        The game: Snake, built on top of all of the above
└── assets/
    └── snake.png       One 96x32 sheet: head, body, food tiles
```

The dependency direction only ever goes one way: `main.cpp` depends on
`engine/`, never the reverse. The engine has no idea what a "player" or an
"arrow key binding" is — that's game code, supplied as a callback.

## How it fits together

**The game loop** (`Engine::run`, in `Engine.cpp`) is the heartbeat. Every
iteration does five things, in this order, forever, until the window closes:

1. **Time** — measure how many seconds elapsed since the last frame
   (`dt`, "delta time"). Every later step multiplies by `dt` so the game
   runs at the same *speed* whether it's rendering at 30fps or 300fps.
2. **Input** — drain SDL's event queue and update `InputManager`'s "which
   keys are held" state.
3. **Update** — run `MovementSystem` (built into the engine), then call your
   game's own logic: either an `onUpdate` callback or the top scene.
4. **Deletions** — destroy every entity queued with `destroyLater()` during
   the update. This happens here, and only here, because it's the one point
   in the frame where nothing is iterating a component pool.
5. **Render** — draw every entity that has a `Transform` and a `Sprite`, then
   every entity that has a `Transform` and a `Text`.
6. **Frame limiting** — if the frame finished early, sleep the rest so the
   loop doesn't spin at 100% CPU.

**The ECS** (`ECS.h`, `Components.h`) is how the world's state is
represented. An `Entity` is just a number — it has no fields and no methods.
Data lives in components: `Transform{x, y}`, `Velocity{dx, dy}`,
`Sprite{width, height, color}`. `World` stores one map per component type
(`entity -> component`) and hands them out on request. A "system" (see
`MovementSystem` in `Systems.h`) is nothing more than a function that loops
over one of those maps and updates it — there's no framework magic beyond
that.

Why bother with this instead of a `Player` class with an `update()` method?
Because game objects rarely fit a clean hierarchy. A crate that becomes a
controllable vehicle when the player enters it isn't cleanly a `Crate` or a
`Vehicle` — it's an entity that gains a `PlayerControlled` component. ECS
makes "mix and match behavior" the default instead of the exception.

**Collision** (`CollisionSystem` in `Systems.h`) answers one question:
which pairs of entities have overlapping `Collider` rectangles? Because
nothing here rotates, that's the AABB ("axis-aligned bounding box") test —
four comparisons per pair. The comparisons are strict (`<`, not `<=`), so
rectangles that merely touch along an edge don't count as overlapping, which
is what makes it usable for grid games where neighboring cells share edges.

Two things about it are worth noticing. First, `Collider` is separate from
`Sprite` on purpose: what a thing looks like and what it hits are different
questions, and games separate them constantly. Second, unlike
`MovementSystem`, the engine does *not* run it for you every frame. Detection
is generic, but *response* — losing a life, eating food, bouncing — depends
on what the entities mean, so game code decides when to ask and what the
answer implies.

**Ticks** (`TickTimer` in `Timing.h`) are the counterpart to `dt`. Multiplying
by `dt` gives smooth motion, which is wrong for anything that happens in
discrete steps: a grid game can't move 0.42 of a cell this frame. `TickTimer`
banks each frame's `dt` and reports how many whole intervals have elapsed —
usually 0, sometimes 1. Crucially it keeps the remainder rather than
discarding it, so ticks don't slowly drift late against a frame rate they
don't divide evenly into.

**Scenes** (`Scene.h`) hold the answer to "what mode is the game in?".
Without them that answer is a pile of booleans — `gameOver`, `paused`,
`showingMenu` — checked in every update, in the right order, and four
booleans describe sixteen states of which most are nonsense. A scene owns one
mode, the engine updates only the top of the stack, and the impossible
combinations become unrepresentable.

It's a *stack* rather than a single current scene because pausing needs the
game underneath to survive: push the pause scene and the playing scene stops
updating but keeps all its entities, so the frozen board still draws behind
the overlay. Pop it and play resumes untouched. `replace` is the other case —
title screen to gameplay, where the old scene should not come back.

Transitions are queued rather than applied immediately, for the same reason
`World::destroyLater` exists: a scene asking to be popped is running inside
its own `update()`, and deleting it there would destroy the object out from
under the call that's executing. The engine applies queued transitions after
the update returns.

**Rendering** (`Engine::render`) is deliberately the *only* place that calls
SDL drawing functions. It draws every `(Transform, Sprite)` pair, then every
`(Transform, Text)` pair. A `Sprite` is drawn one of two ways depending on
whether it carries a texture: as a flat colored rectangle, or as an image (or
one tile of a sheet, if it has a source rect). Both paths honour alpha, so a
translucent panel can dim what's beneath it.

Draw order is `(layer, entity id)`. That matters because component pools
iterate arbitrarily, which is invisible until two sprites overlap and the map
starts deciding which one wins; sorting by an explicit `layer` puts the game
in charge, and the ID tiebreak keeps the order stable between frames so
nothing flickers. Text always draws after every sprite — which is why the
score stays bright over a dimmed board.

**Textures** (`Resources.h`) are loaded through a `TextureCache` the Engine
owns. Ask for the same path twice and you get the same texture; everything it
loaded is freed together when it dies. Paths resolve relative to the
*executable* rather than the working directory (CMake copies `assets/` next to
the binary after each build), so the game behaves the same whether it's
launched from a shell, an IDE, or a double-click. A failed load returns null
and the sprite falls back to a colored rectangle: delete `assets/` and Snake
still plays, in flat colors, exactly as it did before it had art.

**Text** (`Font.h`) is drawn with the same filled rectangles as everything
else, one per lit pixel of a 5x7 font stored directly in the source. The usual
approach is SDL2_ttf plus a `.ttf` file; this keeps the project's "runs with
zero assets" property and adds no dependency, at the cost of one size (whole
number scaling only), uppercase only, and no kerning. Swap in SDL2_ttf the day
you want a real typeface.

**Input** (`Input.h`) turns SDL's raw event stream (a key went down, a key
went up) into two questions game code can ask every frame.
`isKeyDown(SDL_SCANCODE_RIGHT)` is for continuous actions like steering.
`wasKeyPressed(SDL_SCANCODE_P)` is for one-shot actions like pausing, and it
matters more than it sounds: a key stays physically down for six or more
frames, so a menu built on `isKeyDown` would fire six times and toggle itself
back. `Engine::processEvents` is the only place that reads `SDL_Event`s.

**The game** (`main.cpp`) is Snake, and it's where every game-shaped decision
lives: the cell size, the 24x18 grid, the 120ms tick, the four scenes (title,
playing, paused, game over), and the meaning of the three tag components it
invents for itself (`SnakeSegment`, `Food`, `Wall`). `World` stores components
keyed by C++ type, so game code can define its own components without the
engine knowing they exist.

Each snake segment is its own entity with a `Transform`, `Sprite` and
`Collider`; the game keeps them in an ordered list, head first. On each tick
every segment takes the cell of the one ahead of it, the head advances one
cell, and only then are collisions checked — which is why moving into the
cell the tail just vacated is legal, exactly as in the original game. The
border is four wall entities, one per edge, so hitting a wall goes through
the same AABB test as hitting food or hitting yourself.

Notice what it still never does: touch SDL, or write a render loop. It only
describes *what exists* and *what should happen when*.

## Building and running

You'll need CMake, a C++17 compiler, and SDL2 + SDL2_image development
packages. How you get those packages differs by platform — `CMakeLists.txt`
auto-detects which of the two methods below you used, so no manual editing
is needed either way.

### Linux / macOS

SDL2 is installed through the system package manager, which only ships
`pkg-config` files (`.pc`) — no CMake config package. `CMakeLists.txt`
detects this and falls back to `pkg-config` automatically.

```bash
# Debian/Ubuntu
sudo apt-get install libsdl2-dev libsdl2-image-dev cmake g++

# macOS (Homebrew)
brew install sdl2 sdl2_image cmake
```

```bash
mkdir build && cd build
cmake ..
make
./tiny_engine
```

### Windows (Visual Studio / MSVC)

This is the setup your error came from. On Windows there's no system package
manager for C++ libraries, and MSVC doesn't use `pkg-config` the way
Linux/macOS do — `pkg-config.exe` on Windows is usually a leftover from
MSYS2/MinGW, and even if it *did* find an `sdl2.pc`, the libraries it points
to are built for MinGW, not MSVC, so linking would still fail.

The standard fix is **vcpkg**, Microsoft's C++ package manager. It installs
SDL2 as a proper CMake "config package" with ready-to-use targets
(`SDL2::SDL2`), built with the right compiler, and it automatically copies
the needed `.dll` files next to your `.exe` after each build.

```powershell
# 1. Get vcpkg (a one-time setup, anywhere you like — C:\vcpkg is typical)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# 2. Install SDL2 and SDL2_image for 64-bit MSVC
C:\vcpkg\vcpkg.exe install sdl2:x64-windows sdl2-image:x64-windows
```

Then configure the project by pointing CMake at vcpkg's toolchain file, and
explicitly requesting a 64-bit build to match the `x64-windows` packages you
just installed (the Visual Studio generator defaults to 32-bit otherwise,
which would silently mismatch):

```powershell
cd engine_project
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64
cmake --build . --config Release
.\Release\tiny_engine.exe
```

Two things to note if you're new to the Visual Studio generator: `make`
doesn't exist on Windows, so `cmake --build . --config Release` (or opening
the generated `TinyEngine.sln` in Visual Studio and hitting Build) is the
equivalent; and it's a "multi-config" generator, meaning `Debug`/`Release`
binaries land in their own subfolders (`build\Release\`, not `build\`).

A title screen opens: `SPACE` starts a game, `Q` quits. Arrow keys steer,
eating the red food grows you by one segment, `P` pauses, and hitting a wall
or your own body ends the round — the board freezes behind a game-over
overlay so you can see how you died, and `R` plays again. Escape or closing
the window quits from anywhere.

## Where to go from here

Once this structure feels obvious, these are the natural next steps, roughly
in order of how much they'll teach you:

- **Sprite-sheet animation**: `Sprite` already has a source rect, so an
  animated snake needs only an `Animation` component (a list of frames and a
  frame duration) and a system that advances `srcX` over time — the same
  shape as `MovementSystem`, operating on a different field.
- **Audio**: `SDL_mixer`, and a short blip when the snake eats. It is the
  cheapest change on this list and the one that most changes how the game
  feels.
- **Text in the draw order**: text is currently drawn after every sprite, full
  stop, so a translucent panel can dim the board but never the score on top of
  it. Giving `Text` a layer and sorting sprites and text together would fix
  that, at the cost of a slightly busier render loop.
- **A collision broad phase**: `CollisionSystem` currently compares every
  collidable pair, which is O(n²). Bucket entities into a coarse spatial grid
  first and only compare within a bucket — the standard first optimization,
  and one that doesn't change the system's public shape at all.
- **Fixed-timestep physics**: `TickTimer` already does this for game logic;
  `MovementSystem` still runs on the raw frame `dt` (a "variable timestep").
  Look up "fixed timestep game loop" to see how engines run physics on the
  same kind of fixed step, and interpolate between steps when rendering.
- **Packed component storage**: swap `unordered_map` for a "sparse set"
  (a dense array of components plus an index lookup) — same public API,
  much faster iteration, because components end up contiguous in memory.
- **Generational entity IDs**: IDs currently count up and are never reused,
  so they leak ID space, and a stale copy of a destroyed entity's ID silently
  refers to whatever later takes its place. The fix is a free list plus a
  generation counter packed into the ID — which turns `Entity` from a plain
  `uint32_t` into a handle struct, so it trades away "an entity is just a
  number" for safety.
- **A scene/level format**: load entity layouts from a JSON or text file
  instead of hardcoding them in `main.cpp`.

None of these require rewriting what's here — they slot into the same
World/Component/System pattern.
