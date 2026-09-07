# The road to a Cartoon Wars-shaped game

A full slice list, written after reading the Cartoon Wars 2 APK's *structure*
(asset categories, data tables, native library) — not its code and not its art,
neither of which are ours to use.

Slices 1-6 are done; see `v3-plan.md` for what each one found. This is the
whole list, with the finished rows struck through.

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
| ~~**5**~~ **DONE, differently** | **Units that move.** Each unit is a coloured block with an articulated stick figure over it: legs that swing while walking, an arm that sweeps when a blow lands. Built from `Polygon`, whose points the game rebuilds each frame. | **Nothing.** Option 1 from *the thing that is not on this list* below was taken, so no art exists to animate and no sprite machinery was owed. | Open — this is the first slice that cannot be measured. It needs eyes. |
| **5b** | **Frame-based sprite animation**, if real art ever arrives. | **`Animation`** — a component plus a system advancing `Sprite.srcX`. And **`Sprite.flip`**: `SDL_FLIP_NONE` is hardcoded, so a left-facing unit needs a second copy of every frame. | Deferred, not cancelled. Owed the moment there are sprites. |
| ~~**6**~~ **DONE** | **A battlefield worth looking at.** Three bands of hills behind the fight (0.18 / 0.45 / 0.72) and grass tufts in front of it at **1.30**, all placed by a deterministic hash rather than randomly. | **`parallax`** on `Sprite` and `Polygon`, plus `scrollFactor` in `View.h`. Added as a fourth parameter rather than replacing `screenSpace`, because collapsing them would silently invert every existing caller. | Open — needs eyes, like slice 5. |
| ~~**7**~~ **DONE** | **Balance you can edit without a compiler.** `assets/lanebattle/units.txt` holds the roster; it can override any field of any unit and **add new unit types**, and anything it omits keeps its compiled-in value. | **`DataFile.h`** — repeated `[section]` blocks of key/value pairs, paths resolved against the executable like textures, a missing file non-fatal. Also `resetBalance()`, because a roster a file can change is global mutable state and tests must be able to put it back. | Done early: the roadmap said "wait until six unit types", but slices 8 and 9 both want data tables, so the loader earns itself now rather than being rebuilt then. |
| ~~**8**~~ **DONE** | **A castle that fights.** A cannon aimed by clicking the field — press-and-release fires, press-and-drag scrolls — costing 30 gold a shot and reaching 420 pixels. Plus INCOME / WALLS / SUPPLY upgrades at geometrically rising cost, bought by both sides. | **Nothing**, as predicted. It is the first caller of `screenToWorldX`, which slice 4 added and nothing had used. | Yes, but only after a rebalance. Free, long-ranged defensive fire made every game a 800-800 stalemate; charging for shots and halving the reach restored it. See `v3-plan.md`. |
| ~~**9**~~ **DONE** | **A campaign.** Eight stages, each setting the opponent's income, castle health and **composition**; a stage list with locked rows; a win opens the next. Stage tables live in `units.txt`. | **Nothing**, as predicted — the scene stack already did this and the loader was built for it in slice 7. | Yes. The curve took three attempts and every failure was invisible in the table: the first left three stages nobody could win, the second put a wall at stage three because a lean composition beats a padded one. |
| ~~**10**~~ **DONE** | **Progression that persists.** Stages pay out (double on a first clear); the bank buys WEAPONS / RAMPARTS / TREASURY from an armoury on the stage list; all of it survives closing the game. | **`DataWriter`** — the other half of `DataFile`, writing the format it reads — and **`userPath()`**, because a save does not belong next to a read-only executable. **The last item on this list.** | Yes. Perks are keyed by name so reordering the enum cannot scramble a save, and everything a player can edit is clamped on the way in. |
| ~~**11**~~ **DONE** | **Spells.** METEOR, HEAL and RAGE, cast from a **mana** pool that refills on its own — not from gold, which three other things already compete for. Two are aimed and take over the next click on the field. | **Nothing.** The area queries this row predicted turned out to be a distance check the game writes in five lines; `Collision.h` was not needed. | They work and are tested — and measuring showed they change no outcome, because the hero already wins the last stage on its own. The campaign wants a re-tune after slice 12. |
| **12** | **The sky.** Flying units in a second lane, which ground melee cannot reach and only ranged can answer. | Nothing, but it **breaks the one-dimensional assumption** this game has rested on since slice 1 — the first genuine structural change to the rules. | Worth doing only if slice 3 shows that roles make the game interesting. |
| **13** | **Hundreds of units.** | **A spatial grid — but only if `engine_bench` says so.** Targeting is a linear scan per unit per frame, O(n²) overall, and free at present counts. The benchmark says collision costs 12.8% of a frame at 200 entities and 52% at 400. Measure at the real number first. | Is it actually slow, or does it just look like it should be? |

## Five engine additions, total

Across eleven slices the engine gains: ~~**mouse input**~~ (slice 4),
**`Animation` + sprite flip** (deferred — no art to animate), ~~**a parallax
scroll factor**~~ (slice 6), ~~**file reading**~~ (slice 7), and ~~**save/load**~~
(slice 10) — plus a spatial grid if and only if a measurement asks for one, and
it still has not.

**The list is finished.** Ten slices in, the engine has gained four headers:
`View.h`, the mouse in `Input.h`, `parallax`, and `DataFile.h` — which now
writes as well as reads. Five of the ten slices needed no engine code at all.

That is the whole list. Nothing about the ECS, the scene stack, the renderer's
structure, the audio device or the collision system needs rebuilding to get
there, which is a reasonable verdict on the four games that produced them.

## The thing that is not on this list

**Art.** The reference ships 2161 unit images. This project has no artist, and
no slice above is blocked on engine work — several are blocked on pictures.
The realistic options, in the order I would try them:

1. **Articulated polygon units.** ← **taken, in slice 5.** `Polygon`'s points
   are a plain vector, so a unit built from a few line segments can walk and
   swing with no art at all. It cost nothing and needed no engine code. Whether
   it *looks* right is still unanswered.
2. **A CC0 asset pack** (Kenney, OpenGameArt). Free to use and redistribute,
   which the reference's art is not.
3. **Draw it.** The engine loads PNGs and slices sheets already.

Option 1 was taken and slice 5 shipped on it. Options 2 and 3 remain open, and
would make slice 5b (frame-based sprite animation) worth building.

## One structural question, due around slice 9

At that point this stops being a demo that proves an engine and becomes a game
that happens to have one underneath. The repository is currently shaped for the
former: an engine plus four small games that each pull one feature out of it.
Whether Lane Battle should move to its own repository, or the tree should be
reshaped around it, is worth deciding deliberately rather than by drift.
