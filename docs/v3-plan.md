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
