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

## Where it is

**Slice 1 is done**: `src/game/lanebattle/`, 37 checks in
`tests/lanebattle_tests.cpp`. Two castles, gold income, `A` spends it to send a
unit, units march and stop to fight when an enemy is in reach, deaths throw
debris, breaking a castle wins. Title / pause / game-over / restart.

It needed **no new engine code at all**, which was the point of building it
first.

## The open question

Is the loop fun? That gate is unanswered. Play it (`run.bat` → `[3]`) before
building slice 2 — a camera is far more expensive to add than to skip.

Known-thin already: one unit type gets boring quickly, and with symmetric
economies both sides trade evenly until someone's spending rhythm slips.

## Remaining slices

| Slice | The game work | What it forces into the engine |
| --- | --- | --- |
| 2 | Battlefield ~2500px wide instead of one screen | **Camera follow.** The last untested assumption in the renderer: world and screen coordinates finally differ, and `screenSpace` on the HUD stops being decorative. Also a design choice — does the camera track your frontmost unit, the battle line's midpoint, or the player's drag? |
| 3 | Spawn buttons instead of a keypress | **Mouse input.** The engine has none: position, buttons, and "clicked this frame" edges. |
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
