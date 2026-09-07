# v3 — Lane Battle, and what it pulls out of the engine

Working notes for the version in progress. v1.0 is finished and frozen in
`archive/version2/`; this describes what the live tree is doing now.

## The method (unchanged, and the reason any of this worked)

1. **The game pulls features out of the engine — never the reverse.** Rotation
   came from Asteroids, contact normals from Breakout, sub-frame movement from
   a real tunnelling case. Nothing gets built because it might be needed.
2. **Tests are written with the slice, not after it.** Both times that was left
   until later, a bug lived in the gap.
3. **Measure before optimising.** `engine_bench` exists so "it's slow" can be a
   number. It currently says the pairwise collision loop is free below ~100
   entities and unaffordable past 400, and that packed storage would optimise
   something costing 0.11ms at 1600 entities.
4. **Look at it running.** The tests said slice 1 was correct; watching it play
   revealed the enemy wasn't paying for units, which made the game unwinnable.
   Assertions cannot see that.

Slice 2 added a fifth, which is really a sharper version of the fourth:

5. **Simulate a whole session, not just a rule.** Every test in slice 1 checked
   one rule in isolation and all of them passed while the game as a whole could
   not be won. Playing a full battle headlessly found that in seconds.

## Where it is

**Slice 1 is done**: `src/game/lanebattle/`. Two castles, gold income, `A`
spends it to send a unit, units march and stop to fight when an enemy is in
reach, deaths throw debris, breaking a castle wins. Title / pause / game-over /
restart. It needed **no new engine code at all**, which was the point of
building it first.

**Slice 2 is done**: the battlefield is now `kWorldWidth = 2400` — two and a
half screens — and the camera follows your front line, easing towards it,
clamped to the ends of the world. `LEFT`/`RIGHT` take the view off the leash to
scout and hand it back about a second after you let go. A screen-space minimap
marks both castles and both front lines, because the fight can now be happening
entirely off-screen. Unit speed went from 60 to 95: at the old speed crossing
the new field took forty seconds.

It pulled exactly one thing out of the engine, and it is not the camera —
`Camera` and `screenSpace` already existed, and turned out to be right. What
was missing was any way to *test* them: the rule lived inline in
`Engine::render`, which needs a window and a GPU. It now lives in
`include/engine/View.h` as `viewToScreenX/Y`, which `Engine::render` calls, so
`engine_tests` pins the real arithmetic rather than a copy of it.

Before this game, no world was ever bigger than its window. Asteroids moved the
camera a few pixels for screen shake and that was the entire exercise the
renderer's camera path had ever had.

**Slice 3 is done**: three unit types in a table (`kUnitKinds`), keys `1`-`3`,
a population cap of 10, units that queue rather than standing inside one
another, an opponent that banks for a wave and cycles a composition, and gold
paid for kills. It needed **no new engine code**, which is the second time a
Lane Battle slice has demanded nothing — the roster is stats and one extra
comparison, and the queueing rule is a second scan of a list that was already
being scanned.

Two details in it carry most of the behaviour, and both were found by measuring
rather than by design:

- **Only stopped friendlies block, and only if their reach is no longer than
  yours.** The first half lets a fast runner overtake a marching soldier so the
  queue forms only where the fighting is; the second lets a soldier walk past
  its own archers. Without that second comparison the first archer sent walls
  in every melee unit behind it, and ranged units are a trap instead of a
  support.
- **Rank-holding is what stops massing being dominant.** Deleting it on purpose
  makes an army of nothing but runners win, because the whole force occupies one
  pixel and fights as a single enormous unit.


**Slice 4 is done**: a clickable spawn bar along the bottom — one button per
row of the roster, showing name, cost, whether you can currently afford it, and
the shared cooldown draining left to right — plus drag-the-field to scroll. The
number keys still work and always will; they are faster once you know the
roster, and dropping them to add a mouse would be a downgrade.

It pulled two things out of the engine, both predicted:

- **Mouse input** in `Input.h`, shaped deliberately like the keyboard: a held
  query, a pressed-this-frame edge, and a released edge. The edge is not a
  nicety — a mouse button stays physically down for several frames, so a spawn
  button driven by "is it held?" buys a unit per frame. A button event also
  carries its own position, and that is the one used, because a fast click can
  arrive before any motion event.
- **`screenToWorldX/Y`** in `View.h`, the exact inverse of what slice 2 added.
  Written now rather than then, which was the point of leaving it out.

And one thing out of the test harness: `Harness` now synthesises real
`SDL_MOUSEMOTION` and `SDL_MOUSEBUTTON` events, so a headless test clicks a
button the same way a player does and exercises the same edge detection.

Two rules in it are less obvious than they look, and both have tests because
both were easy to get wrong:

- **A press that starts on the spawn bar never becomes a drag.** Without that,
  every click on a button also shoves the camera a few pixels.
- **The gap between two buttons belongs to neither.** A hit test written as
  "the last button whose left edge you are past" silently makes the gaps part
  of the button to their left.

**Slice 5 is done**, but not the way the roadmap predicted, and the difference
is the whole point of it.

The plan said slice 5 would pull an `Animation` component and `Sprite.flip` out
of the engine — the machinery for frame-based sprite animation. It did not,
because **there is no art and no artist**, and building a sprite-animation
system for sprites that do not exist would have been the first speculative
feature this project ever shipped.

Instead each unit is now a coloured block with an articulated stick figure
drawn over it, built out of `Polygon` — whose `points` are a plain vector the
game rebuilds every frame. Two legs that swing while walking, an arm that
sweeps through an arc when a blow lands. No sprite sheets, no files, and
nothing to draw before it works. **It needed no engine code**, which is the
third Lane Battle slice out of five to need none.

`Animation` and `Sprite.flip` stay on the roadmap for whenever real art exists.
They are not cancelled; they are just not owed yet.

Three things in it are worth keeping:

- **The walk cycle advances with distance travelled, not with time.** A runner's
  legs cycle faster than a soldier's without either being told to, and nothing
  ever slides along with its feet still. Driving it from the clock instead is a
  one-word change and looks wrong immediately.
- **A unit is now two entities, which is a leak waiting to happen.** The first
  version relied on an orphan sweep — a figure whose owner has gone gets
  destroyed — and that was *almost* right: deferred destruction meant the
  figure outlived its unit by a frame, so "one figure per unit" was briefly
  false during every fight. A death now takes its own figure with it, and the
  sweep is the safety net rather than the mechanism. The test that found this
  is the only one that catches it.
- **The figure draws on top because it is created after the unit.** Within a
  layer the renderer sorts by entity id, and ids increase in creation order.
  That is a real guarantee, but a quiet one.

What this slice cannot tell you: whether any of it looks right. There is no
window in a test. Every other slice has been measurable; this one is the first
that genuinely needs eyes.

**Slice 6 is done**, and it is the first slice since 2 to pull a real feature
out of the engine.

Three bands of scenery behind the fighting and one in front of it, each sliding
past at its own rate: distant hills at 0.18 of the camera's movement, middle at
0.45, near at 0.72, and grass tufts in the foreground at **1.30** — faster than
the ground, which is the half of depth a boolean could never reach. Positions
come from a cheap integer hash of the index, so nothing is random and every
battle looks the same.

**The engine gained `parallax`** on `Sprite` and `Polygon`, and `scrollFactor`
in `View.h`. `screenSpace` stays, and wins where both are set: the two overlap
arithmetically — `screenSpace` *is* `parallax = 0` — but they say different
things. One means "this is not in the world at all"; the other means "this is
in the world, but far away".

The interesting decision was how to add it. Collapsing `screenSpace` and
`parallax` into a single float is tidier and is a trap: every existing
`viewToScreenX(camera, x, false)` would still compile, because `false`
converts to `0.0f`, while silently meaning its exact opposite — the old `false`
meant "apply the whole camera", the new `0.0f` means "apply none of it". So
`parallax` is a fourth parameter with a default instead. A signature that
quietly inverts its callers is worse than one with an extra argument.

### The regression this slice caused, and what it actually revealed

Adding fifty hills and tufts made the test suite 60% slower — 2.6s to 4.2s.
The cause was not the drawing. `findTargetAhead` and `blockedByFriendly` walked
`world.entities()` and rejected non-combatants by component, so every hill was
considered and discarded once per unit per frame.

That was fine while the world contained nothing but the fight, and stopped
being fine the moment it did not. Both now walk `view<Unit>()` (and
`view<Castle>()` for targets) instead: **4180ms to 1684ms**, which is faster
than before the scenery existed, because the scans had also been walking the
castles, the shards, the figures and the entire HUD.

Worth being clear that this is not the spatial grid arriving early. It is
O(n²) still, in the same place; n is simply now the number of things that can
actually fight rather than the number of things that exist. The grid remains a
later slice and remains contingent on a measurement.

One property changed with it: those scans now iterate an `unordered_map` rather
than the insertion-ordered entity vector, so an exact distance tie between two
targets resolves differently — deterministic within a build, not necessarily
identical across platforms. Nothing depends on the tie-break, and CI runs the
suite on Linux and macOS, which is what would catch it if something ever did.

**Slice 7 is done**, ahead of when the roadmap said it should be. The roadmap's
rule was "only worth doing once the constants genuinely hurt — around six unit
types", and there are three. What changed the arithmetic is that slices 8 and 9
are now definitely happening: stage tables are exactly the kind of data that
belongs in a file, so building the loader now means slice 9 does not have to.

**The engine gained `DataFile.h`** — sections of key/value pairs, read once at
startup. It resolves paths against the executable, the same rule `TextureCache`
already used for textures, so `assets/lanebattle/units.txt` means the same
thing from a shell, an IDE or a double-click.

Three properties were chosen deliberately:

- **A missing or broken file is not fatal.** `load()` reports what happened and
  every getter takes a fallback, so the compiled-in defaults stand and the game
  runs from a bare build directory — the same way a missing PNG leaves a flat
  coloured rectangle rather than a crash.
- **Fallbacks are per field, not per row.** A file that only wants archers
  cheaper says exactly that and nothing else; every other number keeps its
  value.
- **Sections repeat.** That is the whole reason not to use a flat key/value
  file: a roster is a list of things with the same fields, and so is a set of
  stages, which is what will want this next.

Game-side, `kUnitKinds` became `kDefaultUnitKinds` — renamed so any stale use
fails to compile rather than silently reading the wrong table — and the live
roster is a `std::vector` behind `unitKind()`. **A file can add unit types, not
just edit them**, which is the difference between a data file and a config
file. That made the roster's length a runtime value, which in turn needed a
cap on the spawn bar: twelve unit types would otherwise draw four buttons off
the side of the window where nobody can click them.

The roster is global mutable state, which this project otherwise has almost
none of. The price is that tests must be able to put it back, so `resetBalance()`
exists and the test harness calls it before every case. Without that, one test
loading a file changes the meaning of every test after it, and the failure
lands somewhere else entirely.

### Two mutations survived, and both were real gaps

- **Breaking the per-field fallback on `cost` left the whole suite passing.**
  Every test that loaded a file named a unit *and* its price, so the fallback
  was only ever proven for `range`. Each field has its own fallback and so
  needs its own case; there is now a file that sets nothing but `speed`.
- **Breaking comment-stripping left the whole suite passing.** Numbers hide it:
  `strtof` stops at the `#` whether or not the comment was removed, so a broken
  stripper still parses `12` out of `12 # a note`. It only shows on strings —
  and `name` is a string, so a row reading `name = SOLDIER # the front line`
  would define a unit called "SOLDIER # the front line" and silently stop
  matching. There is a string case now.

Both are the same lesson in different clothes: a test that exercises a code
path is not the same as a test that would notice the path being wrong.

## The open question, answered — and then answered again

**Slice 2's verdict: it was not fun, and the reason was not the camera.**
Simulating four minutes of play with the obvious strategy — spend the moment
you can afford to — deadlocked the two armies at *exactly* x=1158 and x=1216,
castles untouched at 800/800, gold cycling on a perfect period, for as long as
the simulation ran. The same thing happened at the old one-screen width, so it
was a slice-1 flaw that slice 2 merely made visible.

The cause was perfect symmetry: the opponent's whole strategy was "spend on
sight", so a player doing the same mirrored it and neither front line moved.

**Slice 3 fixed it, but not on the first attempt, and the failures are the
interesting part.**

Adding three unit types, a population cap, rank-holding and an opponent that
banks made the game *worse* in a new way: measured across eight strategies, the
player lost every single one, and the enemy castle never took a scratch. The
front line marched steadily toward the player's castle because the opponent's
mix included archers dealing free damage from behind its melee, and the player
had no way to build a lasting advantage.

Copying that mix did not help either — it drew, for exactly the reason slice 2
drew. **With income as the only source of gold, two competent sides earn
identically no matter what happens on the field, so a won fight buys nothing
and the line returns to the middle.**

The missing mechanic was **gold for kills**. It is what makes a favourable
trade compound: killing a 95-gold archer with two 35-gold runners now pays, an
advantage on the ground becomes an advantage in the purse, and the purse buys
more units. With it, the measured picture is finally a game:

| Strategy | Result |
| --- | --- |
| All soldiers / all runners / all archers | **Loses**, every time |
| Runners + archers (no front line) | **Loses** |
| Soldiers + archers behind them | **Wins**, ~200s |
| Copying the opponent's composition | **Wins**, ~209s |

No single row of the roster is a strategy, a front line without ranged support
loses, and ranged units without a front line lose fastest. That is the shape
the genre is built on, and `testOneUnitTypeIsNotEnough` now guards it.

Three lessons worth keeping:

1. **Simulating whole battles found all of this.** Every individual rule test
   passed at every stage, including the stage where the game was unwinnable.
2. **Two of the three fixes were wrong before they were right.** Slice 3 was
   built, measured, found broken, and only then completed. Measuring is not a
   verification step at the end; it is how the design got made.
3. **A balance test is worth its fragility.** `testOneUnitTypeIsNotEnough` will
   break if the numbers are retuned badly, which is the point of it.

## Remaining slices

Moved to **`docs/roadmap-cartoonwars.md`**, which lists all eleven of them
after reading what the reference game is actually made of. Keeping the list in
one place stops two copies of it drifting apart.

The short version: slice 3 is unit roles, a population cap and an opponent that
banks — the slice that answers whether the loop has a decision in it — and it
needs no engine code at all. Across the whole roadmap the engine gains five
things: mouse input, animation plus sprite flip, a parallax scroll factor, file
reading, and save/load.

## Deliberately not doing

- **Threading.** A few hundred units is comfortably single-threaded, and the
  measured bottleneck is algorithmic. Adding threads here would be the first
  speculative feature this project has built, and data races are the one bug
  class the tests genuinely cannot see.
- **Packed storage and generational entity IDs.** Both have measurements or
  arithmetic arguing against them. See the README's *Exercises* section.
- **Anything from the Cartoon Wars APK.** The design is fair reference; the
  code and art are not ours to use.
