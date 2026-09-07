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

## The open question, answered

**It was not fun, and the reason was not the camera.** Simulating four minutes
of play with the obvious strategy — hold `A`, spend the moment you can afford
to — deadlocked the two armies at *exactly* x=1158 and x=1216, castles
untouched at 800/800, gold cycling on a perfect period, for as long as the
simulation ran. The same thing happened at the old one-screen width, so this
was a slice-1 flaw that slice 2 merely made visible.

The cause is perfect symmetry. The opponent's whole strategy is "spend the
instant you can afford to", so a player who does the same thing mirrors it
exactly and neither front line ever moves a pixel.

There is a lever, and it is decisive: **bank the gold and send a wave**. The
extra bodies win the trade at the front and the line rolls forward. Banking
just two units' worth wins in about 70 seconds without losing a single point of
castle health — and so does banking six, which is the next problem.

So the loop currently has exactly one decision in it, and it is binary: mirror
the AI and draw forever, or bank anything at all and win crushingly. That is
now pinned by `testABattleCanBeWon`, and the in-game hint no longer teaches the
losing strategy.

**This is what slice 3 should be aimed at, not mouse plumbing for its own
sake.** The shortest routes to a real decision, roughly in order of how much
they'd buy:

- **A second unit type** with a different cost/speed/damage shape, so "what to
  send" joins "when to send it". This is slice 4's work pulled forward.
- **An opponent that banks too**, so massing is answered rather than free.
  Cheap: give it a target wave size instead of spending on sight.
- **A reason not to mass**, such as a rising unit cost or a supply cap, so the
  answer to every situation is not "send more".

## Remaining slices

| Slice | The game work | What it forces into the engine |
| --- | --- | --- |
| 3 | Spawn buttons instead of a keypress | **Mouse input.** The engine has none: position, buttons, and "clicked this frame" edges. Also the inverse of `viewToScreenX` — a click is in screen space and the world it lands on is not. |
| 4 | Several unit types, costs, cooldowns | **A UI layer** — hit-tested regions, cooldown indicators. Starts game-side; only becomes engine code if a second game wants it. |
| 5 | Units that walk and swing | **`Animation`** — `Sprite` already carries a source rect, so this is a component plus a system advancing `srcX`. First real consumer. |
| 6 | Hundreds of units on the field | **Spatial grid**, but only if `engine_bench` says so at real unit counts. Targeting is currently a linear scan per unit per frame — O(n²) overall, and free at this size. |
| 7 | Upgrades that persist between battles | **Serialization.** The last completely untouched category. |

## Deliberately not doing

- **Threading.** A few hundred units is comfortably single-threaded, and the
  measured bottleneck is algorithmic. Adding threads here would be the first
  speculative feature this project has built, and data races are the one bug
  class the tests genuinely cannot see.
- **Packed storage and generational entity IDs.** Both have measurements or
  arithmetic arguing against them. See the README's *Exercises* section.
- **Anything from the Cartoon Wars APK.** The design is fair reference; the
  code and art are not ours to use.
