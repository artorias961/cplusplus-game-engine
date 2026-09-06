# Tiny Engine

A minimal 2D game engine in C++17 + SDL2, built to be read end to end in one
sitting. It's the smallest version of the architecture real engines use: a
game loop, an Entity-Component-System, a layered renderer with textures,
rotation, vector shapes and bitmap text, input handling, box and circle
collision, fixed-tick timing and a scene stack — each in its own file with
heavy comments explaining *why*, not just *what*.

Two games are built on it: **Asteroids**, in `src/game/`, and **Snake**, kept
as a standalone snapshot in `archive/version_1/`. That's on purpose — an
engine with only one game is a hypothesis, not an engine.

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
│   ├── Components.h    Transform, Velocity, Sprite, Polygon, colliders, ...
│   ├── Systems.h       Movement, Lifetime, Collision (boxes and circles)
│   ├── Timing.h        TickTimer (variable frames -> fixed-length ticks)
│   ├── Scene.h         Scene + SceneStack (menu / playing / paused / ...)
│   ├── Font.h          A 5x7 bitmap font, built into the binary
│   ├── Resources.h     TextureCache (load each image once, own it)
│   ├── Input.h         InputManager (keys held, and keys just pressed)
│   └── Engine.h        Window/renderer/game-loop owner
├── src/engine/
│   └── Engine.cpp      SDL setup, the loop, and the built-in render system
├── src/game/
│   └── main.cpp        The game: Asteroids, built on all of the above
├── tests/
│   └── engine_tests.cpp  Asserts about the engine's pure logic
├── assets/
│   └── asteroids.png   Ship icon + rock, for the HUD and title screen
└── archive/
    └── version_1/      Snake: the first game, kept as a standalone project
```

The build produces three targets, and the split is the point:

| Target | What it is |
| --- | --- |
| `engine` | A static library. Knows nothing about any particular game. |
| `asteroids` | The game. Links `engine`. |
| `engine_tests` | Asserts about the engine. Links `engine`. |

Everything used to compile into a single executable, which made "game code
depends on engine code, never the reverse" a rule you had to enforce by
reading. Now the build enforces it: `engine`'s include path contains only
`include/`, and the library is compiled and linked without any game object
files — so an engine file that reaches for game code fails to compile, and one
that calls into it fails to link. Adding a second game is now another
`add_executable` that links `engine` — three lines, not a restructure.

`archive/version_1/` is a complete, self-contained copy of the project as it
stood when Snake was the game — its own `CMakeLists.txt`, engine and assets.
It still builds and plays:

```powershell
cmake -S archive\version_1 -B archive\version_1\build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64
cmake --build archive\version_1\build --config Release
```

The engine there is deliberately frozen and does not receive later changes —
that's what makes it a snapshot rather than a second copy to maintain. The
trade-off is that Snake no longer builds against the current engine, so it
can't catch regressions in it; that job now belongs to `engine_tests`.

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
3. **Update** — run the built-in systems (`MovementSystem`, then
   `LifetimeSystem`), then call your game's own logic: either an `onUpdate`
   callback or the top scene.
4. **Deletions** — destroy every entity queued with `destroyLater()` during
   the update. This happens here, and only here, because it's the one point
   in the frame where nothing is iterating a component pool.
5. **Render** — sprites first (sorted by layer), then polygons, then text.
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

**Collision** (`CollisionSystem` in `Systems.h`) answers one question: which
pairs of entities overlap? Entities carry one of two shapes, and the system
picks the right test for each pair.

A `Collider` is a rectangle, tested with AABB ("axis-aligned bounding box") —
four comparisons per pair. The comparisons are strict (`<`, not `<=`), so
rectangles that merely touch along an edge don't count as overlapping, which
is what makes it usable for grid games where neighboring cells share edges.

A `CircleCollider` is a circle, tested by comparing squared distance against
squared radii — no square root needed. Circles exist because AABB is simply
*wrong* for anything that rotates: turn a ship 45° and its axis-aligned box
either stops covering it or covers empty space. A circle looks identical at
every angle, which is why arcade games full of spinning things use them, and
why it's far cheaper than the real alternative (SAT on rotated polygons).

The two anchor differently on purpose — a box hangs down-right from its
Transform, a circle is centred on it — because that's the natural convention
for each shape, and the mixed circle-vs-box test accounts for it.

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

**Rotation and vector shapes.** `Transform` carries a `rotation` in radians,
and `AngularVelocity` is to it exactly what `Velocity` is to position — both
applied by `MovementSystem`, so a tumbling rock needs no per-frame code.

There are two ways to draw something rotated. A textured `Sprite` goes
through `SDL_RenderCopyEx`, which is `RenderCopy` plus an angle (SDL wants
degrees, so the renderer converts from radians at that one point). A
`Polygon` is an outline of points in *local* space that the renderer rotates
and translates itself, which is all of 2D rotation in two lines:

```
x' = x cos(a) - y sin(a)
y' = x sin(a) + y cos(a)
```

Polygons need no assets, stay sharp at any size, and can be generated at
runtime — every rock in Asteroids has its own lumpy outline made from a few
random numbers. Note that a plain *untextured* `Sprite` ignores rotation
entirely: SDL fills axis-aligned rectangles only, and rotating an untextured
shape is what `Polygon` is for.

**Lifetimes.** A `Lifetime` component counts down and deletes its entity when
it expires, run by `LifetimeSystem` in the engine loop. It's how bullets clean
themselves up without any game-side list of live bullets — and it's the
clearest example of why `destroyLater` exists, since it deletes entities from
inside a loop over the very pool they live in.

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

**The game** (`main.cpp`) is Asteroids, and it's where every game-shaped
decision lives: how hard the ship accelerates, how long a bullet survives,
what a rock breaks into, and the meaning of the three components it invents
for itself (`Ship`, `Bullet`, `Rock`). `World` stores components keyed by C++
type, so game code defines its own without the engine knowing they exist.

Thrust is an *acceleration*: holding Up adds to the ship's velocity rather
than setting it, which is why the ship drifts and has to be flown. Rocks come
in three sizes; a bullet hit scores, deletes the rock, and spawns two of the
next size down, so one large rock is worth seven kills. Bullets are never
tracked in a list — each carries a `Lifetime` and removes itself. Space wraps
at the edges, which is game code, because "the world is a torus" is a rule
about *this* game, not about engines.

**Why a second game matters more than another engine feature.** Asteroids was
chosen to exercise everything Snake couldn't. Snake is grid-locked and
tick-based, so it never used `Velocity`, never rotated anything, and only ever
asked collision a yes/no question. Asteroids is continuous, rotates constantly,
uses circles instead of boxes, and creates and destroys entities every second.
An engine with one game is a hypothesis; the second game is the test — and
everything the engine gained here (rotation, circles, polygons, lifetimes)
was pulled out by a real requirement rather than guessed at in advance.

Notice what game code still never does: touch SDL, or write a render loop. It
only describes *what exists* and *what should happen when*.

## Requirements

| What | Why it's needed | Tested with |
| --- | --- | --- |
| A C++17 compiler | `if constexpr`-era language features, structured bindings, `inline` variables | MSVC 19.4x (Visual Studio 2022, v17.13) |
| CMake 3.15 or newer | generates the build files | 4.4.3 |
| SDL2 2.0.10+ | window, renderer, input, timing | 2.32.10 |
| SDL2_image 2.0+ | loads `assets/asteroids.png` | 2.8.12 |
| Git | only on Windows, to fetch vcpkg | any |

On Windows you also need **Visual Studio 2022** with the *Desktop development
with C++* workload (that's what provides the MSVC compiler and MSBuild; the
free Community edition is fine). CMake ships with that workload, so a separate
CMake install is optional.

No audio library is needed — the game is silent.

### Graphics drivers

The engine asks SDL for a **hardware-accelerated renderer** (Direct3D on
Windows, OpenGL or Metal elsewhere), which needs a working GPU driver:

- **Windows** — the vendor driver for your GPU (NVIDIA / AMD / Intel). A fresh
  Windows install running on "Microsoft Basic Display Adapter" has no 3D
  driver until you install one.
- **Linux** — Mesa (`libgl1-mesa-dri`), plus a running X11 or Wayland session.
- **macOS** — nothing to install; Metal is part of the OS.

None of this is strictly *required*. If SDL can't provide an accelerated
renderer — a VM without 3D acceleration, a remote-desktop session, a
driverless machine — the engine prints

```
Accelerated renderer unavailable (...), falling back to the default renderer.
```

and carries on with software rendering. The game looks identical and plays
fine; it just does the drawing on the CPU and loses vsync. That fallback is
deliberate (see `Engine::Engine`), and it's why this project still runs on
machines where most SDL samples refuse to start.

For a headless machine (CI, an SSH session with no display), set
`SDL_VIDEODRIVER=dummy` and it will run with no window at all.

## Building and running

`CMakeLists.txt` auto-detects how SDL2 was installed — vcpkg's CMake config
package first, falling back to `pkg-config` — so no manual editing is needed
on any platform.

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

Then, from the project root:

```bash
cmake -S . -B build
cmake --build build
./build/asteroids
```

To rebuild after editing a file, only the last two lines are needed — the
first step is one-time setup per machine.

### Windows (Visual Studio / MSVC)

On Windows there's no system package
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
# 3. Configure (one time per machine), from the project root
cd engine_project
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64

# 4. Build, and run
cmake --build build --config Release
.\build\Release\asteroids.exe
```

Every time you change a source file after that, only step 4 is needed.

Two things to note if you're new to the Visual Studio generator: `make`
doesn't exist on Windows, so `cmake --build build --config Release` (or
opening the generated `TinyEngine.sln` in Visual Studio and hitting Build) is
the equivalent; and it's a "multi-config" generator, meaning `Debug`/`Release`
binaries land in their own subfolders (`build\Release\`, not `build\`).

The executable is **not** standalone. Next to it the build puts the SDL
runtime — `SDL2.dll`, `SDL2_image.dll`, `libpng16.dll`, `z.dll` (copied by
vcpkg) — and the `assets/` folder (copied by `CMakeLists.txt`). Copying just
the `.exe` somewhere else will fail to start, or start without artwork. Move
the whole `Release` folder.

### Running the tests

The same build produces a test binary. It needs no window and no GPU, and
finishes in milliseconds:

```powershell
ctest --test-dir build -C Release
```

Or run it directly — `.\build\Release\engine_tests.exe` — which prints how
many checks passed. It covers the collision maths (including the strict
edge-touching rule a grid game depends on), `TickTimer`'s accumulation and
its zero-interval guard, entity creation and deferred destruction, the
movement and lifetime systems, the exact ordering of scene-stack transitions,
and the integrity of every glyph in the font table. Anything needing a window
is deliberately absent: rendering is verified by looking at it.

### Controls

A title screen opens: `SPACE` starts a game, `Q` quits.

| Key | Action |
| --- | --- |
| Left / Right | Turn the ship |
| Up | Thrust (you keep drifting after you let go) |
| Space | Fire |
| P | Pause and resume |
| R | Play again, on the game-over screen |
| Escape | Quit from anywhere |

Shoot the rocks: large breaks into two medium, medium into two small, small
into nothing, scoring 20 / 50 / 100. Clear the field and the next wave arrives
one rock larger. Colliding with a rock costs one of three lives; a fresh ship
flashes while it's briefly invulnerable.

### When it doesn't work

| Symptom | Cause and fix |
| --- | --- |
| CMake: `Could not find a package configuration file provided by "SDL2"` | The toolchain file wasn't passed, or the packages aren't installed. Re-run the configure line with `-DCMAKE_TOOLCHAIN_FILE=...`, after `vcpkg install sdl2:x64-windows sdl2-image:x64-windows`. |
| Linker: `module machine type 'x64' conflicts with target machine type 'x86'` | The `-A x64` was left off, so a 32-bit build is trying to link 64-bit libraries. Delete `build/` and configure again with `-A x64`. |
| `SDL2.dll was not found` on launch | The `.exe` was moved away from its DLLs. Run it from `build\Release\`, or move that whole folder. |
| The game runs, but everything is flat colored squares | `assets/asteroids.png` is not next to the executable. The console says so. Rebuilding re-copies it. |
| `Accelerated renderer unavailable ...` on startup | No 3D driver available; it fell back to software rendering. Harmless — see *Graphics drivers* above. |
| Linux: `No package 'sdl2' found` | The development headers are missing (the runtime library alone isn't enough): `sudo apt-get install libsdl2-dev libsdl2-image-dev`. |
| Nothing opens, no error, over SSH or in CI | There's no display. Either run it locally, or set `SDL_VIDEODRIVER=dummy` to run headless. |

## Where to go from here

Once this structure feels obvious, these are the natural next steps, roughly
in order of how much they'll teach you:

- **Audio**: `SDL_mixer`, and a thump when a rock breaks. It's the cheapest
  change on this list and the one that most changes how the game feels.
- **Sprite-sheet animation**: `Sprite` already has a source rect, so an
  animated sprite needs only an `Animation` component (a list of frames and a
  frame duration) and a system that advances `srcX` over time — the same
  shape as `MovementSystem`, operating on a different field.
- **Collision response**: `CollisionSystem` reports *that* two things
  overlap, never how deeply or from which side. Neither game so far has needed
  more — Snake dies, Asteroids explodes — but anything that bounces or gets
  pushed does. Returning a contact normal and penetration depth is the next
  real step, and a game where things ricochet is what should drive its design.
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
- **A second live game**: the engine currently has exactly one consumer, since
  Snake is a frozen snapshot rather than a target in this build. Now that
  adding a game is three lines of CMake, a second one — Breakout is the
  obvious candidate, since bouncing is what would force collision response
  into existence — would keep the engine honest in a way tests alone can't.

None of these require rewriting what's here — they slot into the same
World/Component/System pattern.
