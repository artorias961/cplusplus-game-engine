# The road to a Cartoon Wars-shaped game

A full slice list, written after reading the Cartoon Wars 2 APK's *structure*
(asset categories, data tables, native library) — not its code and not its art,
neither of which are ours to use.

Slices 1 and 2 are done; see `v3-plan.md`. This is everything after them.

## What the reference actually is

Stripped to its systems, the genre is:

- a one-dimensional battlefield with a castle at each end;
- an economy that pays out over time, spent from a **bar of unit buttons**,
  each with its own cost and cooldown;
- a **population cap**, which is what stops "send more" being the answer to
  every question;
- units with **roles** — cheap swarm, tanky melee, fragile ranged — so
  composition matters and not just count;
- a **castle that participates**, with an aimed shot and in-battle economy
  upgrades;
- **stages** with rising difficulty, and
- **permanent progression** between them, bought with what the stages paid.

Everything else — the art, the factions, the monetisation — is decoration on
that. The APK's proportions say so plainly: 78% of its 2759 images are unit
frames, its largest data file by thirty times is the animation table, and its
balance lives in files exported from a spreadsheet, one of which is called
`XlsBalance`.

## The slices

| # | The game work | What it forces into the engine | The question it answers |
| --- | --- | --- | --- |
| ~~**3**~~ **DONE** | **Make the battle a battle.** Three unit types (runner, soldier, archer) in a table, keys `1`-`3`, a population cap of 10, units that queue rather than standing inside one another, an opponent that banks a wave and cycles a composition — **and gold paid for kills**, which was not in this plan and turned out to be the mechanic the whole design rested on. | **Nothing**, as predicted. | **Yes.** Mono-type loses every time, ranged without a front line loses fastest, and mixed compositions win. See `v3-plan.md` for the measurements and the two wrong turns on the way. |
| ~~**4**~~ **DONE** | **A spawn bar.** Clickable unit buttons showing cost, affordability and the draining cooldown; drag the field to scroll. Number keys kept. | **Mouse input** (held / pressed-this-frame / released) and **`screenToWorld`**, both exactly as predicted — plus mouse events in the test harness, which was not predicted and is what makes any of it testable. | Open. The bar teaches the roster where the keys did not, but whether it is *better* needs a player. |
| **5** | **Units that move.** Walk, attack and death animations; units facing the way they travel. | **`Animation`** — a component plus a system advancing `Sprite.srcX`. `Sprite` already carries a source rect, so this is small. And **`Sprite.flip`**: `SDL_FLIP_NONE` is currently hardcoded, so today a left-facing unit needs a second copy of every frame. | Does it read as a fight rather than as rectangles sliding? |
| **6** | **A battlefield worth looking at.** Three or four background layers scrolling at different rates, a skyline, a foreground. | **A per-layer scroll factor.** `screenSpace` is a boolean today — full camera or none — with nothing in between, which is exactly what parallax needs. | Does the wide field feel like a place instead of a corridor? |
| **7** | **Balance you can edit without a compiler.** Unit stats, costs and stage definitions move out of `constexpr` and into data files. | **File reading** — the engine has none at all today. `Resources.h` loads PNGs through SDL_image; nothing anywhere reads a data file. A small key/value or CSV reader is enough. | Only worth doing once the constants genuinely hurt — around six unit types. Until then it is speculative. |
| **8** | **A castle that fights.** An aimed shot on a cooldown, and in-battle upgrades: income rate, castle health, population cap. | Probably nothing; projectiles are Transform + Velocity + Collider, all of which exist. | Is there anything to do while you wait for gold? |
| **9** | **A campaign.** Eight or so stages, each with its own enemy composition, income multiplier and castle health. A stage-select screen. Win, advance, get harder. | Nothing new — the scene stack already does this. Stage tables ride on slice 7. | Does it survive being played more than once? |
| **10** | **Progression that persists.** Stages pay out; the payout buys permanent unit and castle upgrades and unlocks new types. | **Serialization** — writing and reading a save file. Same machinery as slice 7, which is why these two belong next to each other rather than at opposite ends of the plan. | Is there a reason to come back tomorrow? |
| **11** | **Spells.** Two or three abilities on long cooldowns — a meteor that damages an area, a heal, a rage. | Area queries, which `Collision.h` already answers. | Do the moments of a battle have peaks, or is it flat? |
| **12** | **The sky.** Flying units in a second lane, which ground melee cannot reach and only ranged can answer. | Nothing, but it **breaks the one-dimensional assumption** this game has rested on since slice 1 — the first genuine structural change to the rules. | Worth doing only if slice 3 shows that roles make the game interesting. |
| **13** | **Hundreds of units.** | **A spatial grid — but only if `engine_bench` says so.** Targeting is a linear scan per unit per frame, O(n²) overall, and free at present counts. The benchmark says collision costs 12.8% of a frame at 200 entities and 52% at 400. Measure at the real number first. | Is it actually slow, or does it just look like it should be? |

## Five engine additions, total

Across eleven slices the engine gains: ~~**mouse input**~~ (done, slice 4),
**`Animation` + sprite flip**, **a parallax scroll factor**, **file reading**,
and **save/load** — plus a spatial grid if and only if a measurement asks for
one.

That is the whole list. Nothing about the ECS, the scene stack, the renderer's
structure, the audio device or the collision system needs rebuilding to get
there, which is a reasonable verdict on the four games that produced them.

## The thing that is not on this list

**Art.** The reference ships 2161 unit images. This project has no artist, and
no slice above is blocked on engine work — several are blocked on pictures.
The realistic options, in the order I would try them:

1. **Articulated polygon units.** `Polygon` already rotates around a Transform,
   so a unit built from a few line segments can walk and swing with no art at
   all. It costs nothing, it cannot look half-finished the way bad sprites do,
   and it suits everything else about this project.
2. **A CC0 asset pack** (Kenney, OpenGameArt). Free to use and redistribute,
   which the reference's art is not.
3. **Draw it.** The engine loads PNGs and slices sheets already.

Option 1 makes slice 5 buildable this week. Options 2 and 3 make it a project.

## One structural question, due around slice 9

At that point this stops being a demo that proves an engine and becomes a game
that happens to have one underneath. The repository is currently shaped for the
former: an engine plus four small games that each pull one feature out of it.
Whether Lane Battle should move to its own repository, or the tree should be
reshaped around it, is worth deciding deliberately rather than by drift.
