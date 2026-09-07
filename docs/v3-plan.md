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

**Slice 8 is done**: a castle cannon you aim by clicking the field, and three
in-battle upgrades — INCOME, WALLS, SUPPLY — with geometrically rising costs.
Both sides have both. It needed **no engine code**: a shell is Transform +
Velocity + Polygon, gravity is one line the game applies itself, and the blast
is a distance check.

It is the first thing to use `screenToWorldX`, which was written in slice 4 and
had no caller at all until now — the spawn bar is screen-space, so it
hit-tests against the cursor directly. A shot is different: the click is in
window pixels and the ground it lands on is up to a screen and a half away.

One button does two things. A press that moves more than a few pixels is a
camera drag; a press released in about the same place is a shot. The aim point
is converted to world coordinates at PRESS time, not on release, because the
camera keeps following underneath a held button and would otherwise have
drifted by the time the shot went off.

The launch velocity is solved rather than guessed:

    vx = (x1 - x0) / T
    vy = (y1 - y0) / T - g*T/2

so a click always lands where it was clicked. Firing at a fixed speed and
letting gravity decide would be less code and a worse game — aiming would
become a feel to learn instead of a decision to make.

### The cannon broke the game, and measuring said so precisely

The first version was free, on a cooldown, permanent, and — for the opponent —
perfectly aimed. A mixed army that had won in 195 seconds now **lost** in 247.
Every good strategy drew 800-800 with both castles untouched.

Four variants isolated it in one run:

| Variant | Result |
| --- | --- |
| As built | **LOST** at 247s |
| Cannon range set to 0 (nobody's gun reaches) | **WON** at 195s |
| Cannon damage set to 0 | **WON** at 194s |
| Upgrades priced out of reach, cannons left on | **LOST** at 247s |

So it was the cannon alone; upgrades were innocent. The reason is structural
rather than numerical: **free defensive damage that never runs out makes the
approach to a castle a killing field nobody can cross**, so defending beats
attacking for both sides and the front line parks in the middle forever.

Two changes fixed it. A shot now **costs 30 gold**, which turns "how much
damage does the cannon do" into "how much damage per gold" — a number that can
be compared against a unit and therefore balanced, and which means a side that
shells constantly fields a smaller army. And its **reach dropped from 780 to
420**, less than half the distance to the middle, so it defends the approach to
your own castle rather than contesting the field.

After that, measured across strategies: mixed armies win, an income upgrade
makes you win *faster* (195s to 138s), and mono-type armies lose while the
opponent snowballs upgrades against them (it finished one such game on 5/5/4).

### A quieter bug in the same area

With shots costing gold, the opponent stopped upgrading entirely — 0/0/0 across
a four-hundred-second game. Its cannon reserved gold for its next wave (about
200) while its upgrade rule reserved for a wave *and* the upgrade (about 290),
so the cheaper commitment always won the race and every surplus went down the
barrel. It lost at full health, which looked like balance and was an ordering
mistake. It now shells only out of what is left once both are covered.

### Six of seven mutations caught, and the survivor was the important one

Setting the cannon's reach ten times too far left every test passing. The range
test stood a unit at 1600 and aimed at where it *stood* — but a soldier walks
eighty pixels during the shell's flight, so it stepped out of the blast whether
or not the shot could reach. The test was measuring the lead error, not the
clamp. It now aims with lead, so the only thing that stops the shell is the
range it was supposed to be testing.

## An audit before slice 9

Eight slices in, a pass over the whole thing looking for what was broken or
slow. The headline is that **nothing needed optimising**, and that is a
measurement rather than an opinion:

    a full 195-second battle: 11672 frames in 484 ms
    = 0.0415 ms per frame = 0.25% of a 16.6 ms budget
    peak 141 entities, peak 16 units

`engine_bench` could not answer this, which was itself a defect: it measured
`CollisionSystem`, which Lane Battle deliberately does not use, so deciding
"does the targeting scan need a spatial grid yet?" meant writing a throwaway
probe. It now has a `scan` column measuring the shape the game actually runs —
for each entity, walk them all and keep the nearest one ahead:

    entities | collision ms | scan ms
         100 |        0.592 |   0.017
         400 |       11.588 |   0.357
         800 |       43.514 |   2.429
        1600 |      183.293 |  15.016

Asking about distance rather than overlap is about thirty times cheaper at
every size. At Lane Battle.s hundred-and-forty entities the scan costs roughly
0.03ms. **The spatial grid is owed somewhere north of eight hundred entities,
not one hundred and forty**, and that is now a command anyone can run rather
than a probe I wrote and deleted.

Five things were fixed.

**1. Text rendering was batched.** The bitmap font drew one
`SDL_RenderFillRect` per lit pixel, and a comment in `Engine.cpp` had said for
four games that the fix was well trodden and not yet worth doing. It became
worth doing when the fourth game's HUD reached about 187 characters on screen —
roughly 3,200 draw calls a frame, 190,000 a second, to render a scoreboard. The
rects for a string are now gathered and handed to SDL in one
`SDL_RenderFillRects`: about 17 calls a frame instead of 3,200, drawing exactly
the same pixels. This is the only part of the audit that could not be measured
here, because there is no window in a test.

**2. Two pieces of dead code removed.** `playSpawn()` was orphaned in slice 3
when spawn sounds became per-unit-kind; `unitKinds()` was an accessor added in
slice 7 that nothing ever called. Neither produced a compiler warning.

**3. Target ties are now decided by the lower entity id.** The targeting scans
walk hash maps, so "the first one found" is an implementation detail that
differs between standard libraries — the same battle could pick a different
target on Linux than on Windows. Nothing depended on it, which is exactly why
it would have been unpleasant later: the bug arrives on a platform this machine
cannot run, in a test that passes here every time.

The first test written for this was worthless and the mutation said so. It ran
the same scenario six times and checked the answers agreed — which they did
with or without the rule, because hash order on one machine is already stable.
The test now asserts the guarantee itself: the lower id wins, with padding
entities to shift the pair between attempts.

**4. Upgrade numbers moved into the data file**, alongside the unit stats, via
`[upgrade]` sections. Their COUNT stays fixed, and that is a distinction rather
than an inconsistency: the roster can grow because the game treats every unit
row the same way, whereas each upgrade has its own rule in code — INCOME
changes a rate, WALLS heals a castle, SUPPLY raises a cap — and there is no
generic "apply upgrade N". A file naming an upgrade the game does not have is
a typo, not a feature, and is ignored.

**5. `testTheShippedRosterIsSane` was reconsidered and left alone.** The worry
was that it depends on CMake copying assets next to the test binary, verified
only on Windows. Re-reading it, a failed copy produces exactly one clear
failure — "the shipped roster file is where the game expects it" — and the
content checks then run against the compiled defaults and pass. The failure
mode was already precise. The risk was overstated.

Two things were looked at and deliberately not changed:

- **`TickTimer` has no live consumer.** Snake used it and Snake is archived.
  It is tested, it is v1.0, and a fixed timestep is real engine knowledge;
  deleting it would tidy away the thing the archive exists to preserve.
- **`frontLineX`'s final fallback is unreachable and mutation-proof.** It is a
  `return` the compiler requires, and it is commented as such rather than left
  looking covered.

## The renderer is tested now

The audit ended by admitting one thing could not be verified: rendering. There
is no window in a test, so for four games the part that decides what a player
actually *sees* was checked by looking at it. A sign error in the camera, a
layer sorted the wrong way, a parallax factor on the wrong axis — each would
have shipped until a human noticed.

That is closed. **`tests/render_tests.cpp` draws real frames and reads the
pixels back**, using SDL's `dummy` video driver and the software renderer:
no window, no GPU, no display, but a real back buffer that
`SDL_RenderReadPixels` can return as a grid of numbers. It is a ctest test like
any other, so CI already runs it — no new job, no new step.

The engine gained two public methods for it, and both are worth having anyway:

- `drawWorld(World&)` — everything `render()` does except presenting, so the
  frame stays readable.
- `captureFrame(width, height)` — the back buffer as ARGB pixels. This is a
  screenshot function; that it is also what makes rendering testable is the
  happy part.

Thirty-two checks now pin things nothing could previously state:

- a sprite paints its colour at its position, and nothing outside its edges;
- the camera shifts world sprites by its negative and leaves `screenSpace`
  ones alone — the rule `View.h` exists to state, checked against pixels
  rather than against itself;
- parallax 0.5 shifts by half the camera and 1.5 by half again;
- a higher layer covers a lower one, and swapping them swaps the answer;
- alpha actually blends, four games after the blend mode was set up;
- **a drawn glyph matches the font table pixel for pixel** — which is what
  finally proves the batched text path draws the same picture the
  one-rect-per-pixel path did;
- entities with a drawable but no `Transform` are skipped rather than
  dereferenced.

### The tests were then attacked, and two rounds were needed

Six deliberate breaks of the renderer:

| Mutation | Caught |
| --- | --- |
| Camera sign flipped | yes |
| Parallax ignored | yes |
| Layer order reversed | yes |
| Sprites without a Transform no longer skipped | yes — segfault |
| Alpha blending turned off | yes |
| Scale dropped from a glyph's x offset | **no**, first time |

The last one is the interesting failure. The scale check probed the first lit
column of the letter `I`, which is column zero — where `col * scale` and `col`
are both nought. A renderer that squashed every string to a third of its width
passed. Spacing is only visible in the *distance between* columns, so the test
now measures the glyph's painted width instead of probing one pixel of it.

Correcting it broke it in the opposite direction first: the width scan swept
far enough to swallow the second character and measured both glyphs as one,
failing against a perfectly correct renderer. Narrowing the scan to where the
next character begins fixed it, and the mutation is now caught.

Which is the same lesson as the data-file slice, for the third time: **a test
that exercises a code path is not the same as a test that would notice the path
being wrong.** The only reliable way to tell the two apart is to break the code
on purpose and watch.

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
