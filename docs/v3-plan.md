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

One section per slice, in the order they were built. Each says what shipped,
what it pulled out of the engine, and — usually the useful part — what
measuring it revealed that was not planned.

### Slice 1

**Done**: `src/game/lanebattle/`. Two castles, gold income, `A`
spends it to send a unit, units march and stop to fight when an enemy is in
reach, deaths throw debris, breaking a castle wins. Title / pause / game-over /
restart. It needed **no new engine code at all**, which was the point of
building it first.

### Slice 2

**Done**: the battlefield is now `kWorldWidth = 2400` — two and a
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

### Slice 3

**Done**: three unit types in a table (`kUnitKinds`), keys `1`-`3`,
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


### Slice 4

**Done**: a clickable spawn bar along the bottom — one button per
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

### Slice 5

**Done**, but not the way the roadmap predicted, and the difference
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

### Slice 6

**Done**, and it is the first slice since 2 to pull a real feature
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

#### The regression this slice caused, and what it actually revealed

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

### Slice 7

**Done**, ahead of when the roadmap said it should be. The roadmap's
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

#### Two mutations survived, and both were real gaps

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

### Slice 8

**Done**: a castle cannon you aim by clicking the field, and three
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

#### The cannon broke the game, and measuring said so precisely

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

#### A quieter bug in the same area

With shots costing gold, the opponent stopped upgrading entirely — 0/0/0 across
a four-hundred-second game. Its cannon reserved gold for its next wave (about
200) while its upgrade rule reserved for a wave *and* the upgrade (about 290),
so the cheaper commitment always won the race and every surplus went down the
barrel. It lost at full health, which looked like balance and was an ordering
mistake. It now shells only out of what is left once both are covered.

#### Six of seven mutations caught, and the survivor was the important one

Setting the cannon's reach ten times too far left every test passing. The range
test stood a unit at 1600 and aimed at where it *stood* — but a soldier walks
eighty pixels during the shell's flight, so it stepped out of the blast whether
or not the shot could reach. The test was measuring the lead error, not the
clamp. It now aims with lead, so the only thing that stops the shell is the
range it was supposed to be testing.

### Per-unit cooldowns (parity with the reference)

Not a roadmap slice — a mechanic the reference game has that this one did not,
found by comparing the two rather than by building the next thing.

Every unit kind now has **its own** cooldown, per side, instead of one shared
0.35-second gap. The button fill shows that kind's timer rather than a global
one, and `cooldown` is a column of the roster table like every other number.

It looks like a detail and is not. With a single shared timer the only limit on
spending was gold, so a banked purse went entirely into whichever unit was
best, and composition was a preference. Per-kind timers mean a large purse
**cannot** be spent on one type — using it means sending something else. They
barely bind at base income (a soldier takes four seconds to afford and two to
recharge) and start mattering exactly when the player has money to burn, which
is when a decision is worth having.

Measured across compositions afterwards:

| Strategy | Result |
| --- | --- |
| Soldiers with archers behind | **Wins** |
| Soldier-heavy plus archers | **Wins** |
| All three kinds in even rotation | **Loses** |
| Any single kind | **Loses** |

The design guard still holds — no one unit type is a strategy — and there is a
new distinction underneath it: the *ratio* matters, not just the mix. An even
three-way split loses where a soldier-heavy line with archers wins, because a
population cap makes quality per slot beat gold efficiency. Runners are
currently a rush unit rather than a mainstay; whether that is right is a
balance question, and balance is a text file now.

#### The same bug, twice, in two places

Both the opponent and the test helper that plays a battle took only the NEXT
entry in their composition — so a cycle opening with two soldiers spent most of
its time waiting out the first soldier's cooldown instead of sending the archer
behind it. Both now read forward for something they can actually send.

The player-side version is the more interesting of the two: left alone, it
would have measured a bad *player* and reported it as a bad *game*. The first
run after the change said the mixed strategy no longer won, which looked like a
balance regression and was a helper that had stopped playing properly.

### Slice 9

**Done**: a campaign. Eight stages, a list you pick from, locked rows you can
see but not play, and a win that opens the next one. **No engine code**, as
predicted — the scene stack already did this, and the stage tables rode on the
`DataFile` loader slice 7 built early for exactly this reason.

Each stage sets the opponent's three levers: how fast it earns, how much castle
it has, and **what it sends**. That last one is what makes stages feel
different rather than merely harder — a stage fielding only soldiers is
answered by archers behind a thin line, one fielding archers of its own has to
be rushed before they set up. Turning a difficulty number up would have
produced eight identical fights.

Progress is not saved: close the game and the campaign restarts. That is slice
10, and doing it here would have been building half of a feature whose other
half does not exist.

#### The curve was measured three times before it was a curve

Playing all eight stages with three different armies takes one run and says
more than reading the table ever could.

**First attempt** — income 0.75 to 1.60, castle 600 to 1500:

    1 THE BORDER      soldiers DRAW   mixed WON
    2..5              soldiers LOST   mixed WON
    6,7,8             soldiers LOST   mixed LOST

Three stages nobody could win, and an opening stage a naive army could only
draw. A campaign whose first level does not teach and whose last three cannot
be finished.

**Second attempt** raised the numbers more gently and produced something worse
in an interesting way: **stage three became a wall while stage four was
comfortable.** Stage three fielded a lean `1,1,2` and stage four a `1,1,2,0`
padded with a runner. A weak unit in the cycle spends gold that would have
bought a soldier, so the higher-income stage bought a *worse* army.

**Composition matters more than either dial.** Early stages are diluted with
runners on purpose now, and the last ones are lean.

**Third attempt**, and the shape it settled into:

| Stage | Soldiers only | Mixed | With two income upgrades |
| --- | --- | --- | --- |
| 1 | **Wins** | Wins | — |
| 2 | Draws | **Wins** | — |
| 3-7 | Loses | **Wins** | — |
| 8 | Loses | Loses | **Wins** |

Which reads as a teaching order without anyone having designed one: stage one
teaches the button, stage two that one unit type is not enough, stages three to
seven that composition wins, and stage eight that composition alone does not —
it wants two income upgrades behind it. One upgrade is *worse* than none, so
the last stage is a genuine commit-or-don't.

#### Three bugs the campaign exposed in older tests

- **A test misnamed since slice 1.** `testRestartingAfterDefeat` broke the
  *enemy* castle, which is a win. Nothing noticed for eight slices because a
  win and a loss did the same thing; they no longer do, so it is two tests with
  the names they should always have had.
- **`testTheEconomiesAreSymmetric` asserted something the campaign
  deliberately breaks.** It now checks the opponent earns the player's rate
  times the stage's multiplier — symmetric up to exactly one dial, which was
  always the real claim.
- **The design guard was measuring the wrong stage.** `testOneUnitTypeIsNotEnough`
  ran on stage one, which mono-type armies are *supposed* to win — that is the
  on-ramp. It runs mid-campaign now, and a second test guards the on-ramp from
  the other side.

#### And one mutation that survived

Dropping every retry back to stage one passed cleanly, because the test that
checked it retried stage one. Retrying a later stage cannot confuse "restart
this stage" with "restart at the beginning". Sixth of six caught after that.

### Slice 10

**Done**: a campaign that persists, money that outlives a battle, and an
armoury to spend it in. This is the **last item on the roadmap's engine list**.

**The engine gained the other half of `DataFile`**: a `DataWriter` that emits
the format `DataFile` reads, and `userPath()`, which asks SDL where this
platform keeps a user's files. Everything else in this engine resolves paths
against the executable, which is right for things that ship with the game and
wrong for a save — the folder a game is installed into is frequently read-only.

Saving deliberately uses the SAME format as every other data file rather than
something terser or binary. A save you can open, read and correct by hand is
worth more than a compact one in a project this size, and one format means the
reader was already tested by everything that reads a file.

Three permanent upgrades, bought between battles from money the campaign paid
out: WEAPONS (+10% damage), RAMPARTS (+150 castle) and TREASURY (+40 starting
gold). They touch things the in-battle upgrades do not, so the two systems are
not the same choice at different speeds. A stage pays more the later it is and
**double on its first clear**, so pushing forward beats farming without ever
forbidding it.

Two details that are easy to get wrong and were:

- **Perks are written by NAME, not by position.** Reordering the enum would
  otherwise turn everyone's weapons into ramparts.
- **Everything a player edits is clamped on the way in.** A save file is a text
  file; `stages_unlocked = 900` opens the campaign rather than indexing off the
  end of the stage list, and a negative bank becomes zero.

#### The tests were quietly overwriting a real campaign

Winning a battle saves, and these tests win a great many battles — so for one
build they were steadily writing over the save of anyone who ran them. The test
harness now points saving at a scratch file next to the binary.

Worth noting what is NOT a leak: the game binary creates an empty save folder
on startup, because `SDL_GetPrefPath` makes the directory as a side effect of
being asked where it is. That folder appearing during a smoke test looked like
the same bug and is not one.

#### Eight mutations, and the one that survived

"Pay the first-clear bonus every single time" passed cleanly. The payout test
won each stage exactly once, which is precisely the case where the two rates
cannot be told apart. There is now a test that wins an already-cleared stage
and checks it pays the ordinary rate — and still pays *something*, so farming
stays possible and stops being the best way to earn.

### The hero

Not a roadmap slice. The roadmap was built by reading the reference game's
asset structure, which shows units and animation but does not shout "one of
these is under the player's control" — so the mechanic its subtitle is named
after was missing from the plan entirely.

**One summon per battle. If it falls, it stays fallen** until the stage is
finished or started again. No respawn, no second chance, no cost and no
cooldown.

That single rule is the whole design, and it is a deliberate change from the
reference. A hero you can re-summon is an ability on a timer, and the only
question is whether the timer has run out. A hero you get *once* is a decision
about when to spend it — which is the interesting question, and the one this
version asks.

It is a roster row like any other, so it walks, fights, queues, animates and is
targeted by the same code as everything else. Three exclusions are what make it
a hero rather than a rich soldier: it is never sold on the spawn bar, never
takes a number key, and is filtered out of stage compositions — a wave cycle
asking for heroes would field a stream of them.

A fourth permanent upgrade, CHAMPION, makes the hero and only the hero
stronger. It stacks on top of WEAPONS rather than replacing it, so the two are
not the same purchase.

#### Measuring it found the thing that makes it work

Played on the last stage, where the margin is thin enough to show:

| Hero summoned | Result |
| --- | --- |
| never | **Lost** |
| on the opening frame | **Lost** |
| after ten seconds or more | **Won** |

**Throwing the hero out immediately is exactly as good as never using it.**
With no line to fight behind it is surrounded and killed for nothing; held
until the front has formed, the same hero wins a stage that a good army loses.
That is the decision the one-summon rule exists to create, and it was not
designed in — it fell out of the rule and was found by playing it.

`testWhenYouSpendTheHeroDecidesWhetherItWasWorthIt` pins all three rows. It is
a balance assertion and fragile on purpose: if it ever fails, the hero has
stopped being a decision and become a button you press when it lights up.

#### Three bugs it exposed, and one mutation that survived

- **The spawn bar stopped at the hero instead of skipping it.** Simpler, and
  wrong: the hero is the last built-in row, so a data file adding a unit after
  it would have put that unit permanently out of reach. Slots and roster
  indices are now different numbers, which is why `kindForButton` exists.
- **The shipped-roster sanity test required every unit to cost something.**
  The hero costs nothing by design — what limits it is that there is one.
- **"CHAMPION boosts every unit" passed cleanly.** The test checked the hero
  hit harder but never that an ordinary soldier did not, which would have made
  the hero perk into a second WEAPONS.
- **The hero button sat on top of the spawn bar.** Both positions were
  independent constants and the hero's covered what would have been the fourth
  and fifth spawn slots. With the built-in roster there is no fourth slot, so
  nothing showed it — but a data file adding one more unit type would have put
  its button underneath the hero's, and the hero is hit-tested first, so
  clicking that unit would have summoned the hero instead. Found by looking
  again rather than by any test, which is the uncomfortable part: it was
  latent, it was one data file away from being real, and the suite was green.
  The two constants are derived from each other now, and a test walks every
  slot the bar can ever draw and checks none of them is read as the hero.


### Slice 11

**Done**: three spells — METEOR, HEAL and RAGE — cast from **mana**, which is
its own pool and refills on its own. **No engine code**: the area queries the
roadmap predicted turned out to be a distance check the game could write in
five lines, and `Collision.h` was not needed either.

Mana rather than gold, deliberately. Gold is already fought over by units,
in-battle upgrades and cannon shots; a fourth claimant would have made every
spell a decision about whether to have an army. Mana buys nothing else, so a
spell is never a sacrifice — the only question is which and when.

Two of them are aimed. Arming one takes over the next click on the field, the
same click that otherwise fires the cannon, so one mouse button now serves
four verbs: drag the view, fire the cannon, cast a spell, press a panel.

#### One rule replaced two bugs

That click had a test naming the panels it must not fall through: the spawn bar
and the upgrade panel, and nothing else. So **clicking the hero button also
fired the cannon**, and **clicking a spell row armed the spell and instantly
cast it into the panel** — arming and casting in one press.

Both were the same mistake, made twice, because every new panel was another
chance to forget. There is one `isOverUi()` question now, asked in one place.
Adding a panel means adding it there, once.

#### And the finding that matters more than the slice

Measured across spell policies, every one of them won, and the spells made no
difference to any outcome. The reason is not the spells:

    the last stage, by what the player uses
      army only            LOST
      army + hero at 20s   WON
      army + 2 income      WON

**The hero alone now wins the capstone.** The difficulty curve was measured in
slice 9, before the hero and the spells existed, and two power sources have
been added on top of it since. The stages are not broken — they are simply
tuned for a player who has neither.

The design guards still pass because they measure a hero-less, spell-less
player, which is exactly the blind spot: a test that fixes the strategy cannot
notice the strategy getting stronger.

**The campaign wants one re-tuning pass**, and it is worth doing AFTER slice 12
rather than now — flying units are another power shift, and tuning the curve
twice would waste the first attempt.

### Slice 12

**Done**: the sky. A GRIFFIN that flies, an ARCHER and a HERO that can reach
it, and everything else that cannot. **No engine code**, as predicted — but it
is the slice that ends the one-dimensional assumption the game has rested on
since slice 1.

Everything until now decided on x alone: who is ahead, who is in reach, who
blocks whom. Altitude adds a question distance cannot answer — *can* this unit
even be attacked by that one — and it is a category rather than a measurement.
**A soldier standing directly beneath a griffin is as close as two things can
be and still cannot touch it.**

Range stays horizontal on purpose, and that is a real decision rather than a
shortcut. Measuring it as a true 2D distance shrinks an archer's 135 to about
28 pixels of horizontal reach against something 130 above it, which makes the
one unit that answers flyers unable to answer them.

Two flags on a roster row carry all of it — `flying` and `hitsAir` — plus one
rule in the queue: only units in the same lane are in each other's way.

#### The payoff was the opposite of what was assumed

The obvious guess was that adding archers behind a line of soldiers answers
the sky. Measured against a stage of nothing but griffins:

| Player's army | Result |
| --- | --- |
| Nothing but soldiers | **Loses** |
| A line of soldiers with archers behind it | **Loses** |
| Nothing but archers | **Wins**, 800 to 0 |

Against an entirely airborne enemy every soldier is gold and a population slot
spent on something that can reach nothing, so ground melee is not merely
useless — it actively costs the battle. Which makes the sky a genuine
rock-paper-scissors answer rather than a tax: the counter to all-air is to
**stop building the units that normally carry you**.

The test asserting this was written the other way round first, and the
measurement corrected it.

#### Two mutations survived, and both were tests measuring the wrong rule

- **"The two lanes share one queue" passed cleanly.** The shipped griffin
  reaches 40 and a soldier 34, and a friendly whose reach exceeds yours never
  blocks you anyway — so the test was measuring the RANGE rule and calling it
  the lane rule. It now defines a short-ranged flyer through a data file, and
  asserts against the soldier's own attack position rather than a
  hundred-pixel margin that could not tell fifteen pixels of difference.
- **"The figure hangs off the ground line" passed cleanly.** Pinning a stick
  figure to `kGroundY` was the same thing as pinning it to its owner's feet
  for every unit in the game — right up until one of them left the ground. A
  griffin drawn with its legs dangling a hundred and fifty pixels beneath it
  is visible only to eyes, so there is a test for it now.

## Between slices: an audit, and closing the last untested gaps

### The audit

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

### The renderer is tested now

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

#### The tests were then attacked, and two rounds were needed

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

### And the games are started now, too

Closing the renderer gap exposed a second one directly behind it. Every game's
RULES are tested by driving its scenes through `tests/Harness.h` — but the
harness reimplements the loop rather than using it. That left a stretch of the
project executed by nobody at all: **`main.cpp`, the real `Engine::run`, window
creation, texture loading, audio startup and shutdown**. A game that crashed on
its first frame, or could not find its assets, would have passed every test in
the suite.

`TINY_ENGINE_MAX_FRAMES` closes it. Set it and the loop runs that many frames
and exits; with `SDL_VIDEODRIVER=dummy` alongside, that happens on a machine
with no display. Three ctest entries now start each shipped executable for half
a second and require exit 0. It is the shallowest test in the project and it
covers its deepest untested seam.

It was proved by breaking the thing only it can see: deleting the
software-renderer fallback in the `Engine` constructor, so the engine throws on
any machine without an accelerated backend — which is every CI runner.

    engine_tests      Passed        breakout_tests    Passed
    asteroids_tests   Passed        lanebattle_tests  Passed
    render_tests      Passed
    asteroids_starts  EXCEPTION     breakout_starts   EXCEPTION
    lanebattle_starts EXCEPTION

Five suites green, three binaries dead. Note `render_tests` passing there:
it skips deliberately when the Engine cannot be constructed, so that a machine
which genuinely cannot render reports a skip rather than a failure. The price
of that choice is that it cannot catch this, which is exactly why running the
real binaries is worth its own test.

They are labelled `smoke` so the pipeline runs them once rather than twenty
times. The repeated run exists to shake out flaky *simulation*; starting a
process is not that kind of test, and repeating it had doubled the pipeline
from 94 seconds to 183 for no information. CI now does:

    ctest -LE smoke --repeat until-fail:20     the flake hunt
    ctest -L  smoke                            the games start

#### And CI itself was finally checked

Every claim in these notes about Linux and macOS had been inference — this
machine is Windows and nothing here had ever seen a pipeline result. The GitHub
API says the last completed runs on `main` are **green on both platforms**, so
the inference was sound and the hedging can stop.

## How the design was found: the balance history

The slice notes above say what was built. This says how the game stopped being
unplayable, which took three goes and was never once found by a unit test.

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

## A second audit, after twelve slices

The first audit found nothing that needed optimising. This one found four
things wrong, and the useful part is what kind of wrong they were: none was
visible by reading, all four were reachable by a player, and every one had a
test sitting next to it that was happy.

### HEAL was subtracting health from the hero

`castHeal` capped healing at `kindOf(unit.kind).health` — the roster's number.
That is the right ceiling for every unit the bar sells and the wrong one for
exactly one thing: a hero carrying CHAMPION, whose health is scaled on the way
out of the gate. A championed hero above the roster's number got **clamped
down** by its own heal. Cast on a hero at 700 with a maximum of 775, HEAL took
it to 620.

Only a player who had paid for the perk could see it, which is the worst
possible distribution for a bug. `testHealRestoresYourUnitsButNotBeyondFull`
could not: a soldier's maximum and its roster row are the same number, so the
case that breaks is the one case the test could not contain.

The fix is a `maxHealth` field on `Unit`, set at spawn and scaled with the
hero. A cap now asks the unit rather than the table.

### The number keys and the spawn bar were different lists

The keys were bound to roster rows, four of them, not skipping the hero. The
bar is bound to slots, five of them, skipping the hero. With the shipped
roster the two agree by coincidence.

Add a sixth sellable unit type in `units.txt` and they stop agreeing: the bar
is full and the keys have run out, so that unit is purchasable by **nothing at
all**. `testTheSpawnBarStopsAtTheEdgeOfTheWindow` watched this happen and
approved, because truncating the bar is what the bar should do — the question
it never asked was whether the keyboard still covered what the bar showed.

The header claimed the opposite in a comment: *"their number keys still work,
so nothing is unreachable."* Both are fixed — the keys are bound to
`kindForButton` now, there are five of them, and a `static_assert` ties the
count to `kMaxVisibleButtons` so they cannot drift apart again.

This is the second bug of exactly this shape, after the hero button that
overlapped spawn slots four and five. Both were one data file away from real,
and both came from two things deriving the same fact separately.

### mutate.bat did not run

The tool this project calls its most useful technique failed on **its own
documented example**, with a wall of `'m' is not recognized as an internal or
external command`.

Line endings. `cmd.exe` does not read a batch file line by line — it seeks by
byte offset between commands, and an LF-only `.bat` makes it land mid-line and
run the tail of one as a command. Nothing in the error says so, and the
obvious comparison pointed the wrong way: `verify.bat`, in the same folder and
just as LF, runs perfectly.

Worse, it worked for everyone else. `core.autocrlf` is true, so a fresh clone
gets a CRLF copy that runs — the tool was broken only in the working tree of
the person who wrote it. A `.gitattributes` now pins `*.bat` to CRLF.

The first guess was wrong, too, and worth recording: the `^` line continuation
looked like the culprit, removing it changed nothing, and only writing a CRLF
copy and watching it parse settled it.

### Nine full-world scans a frame

Slice 6 moved `findTargetAhead` and `blockedByFriendly` off `world.entities()`
and onto `world.view<Unit>()`, and recorded why: fifty hills and tufts,
rejected once per unit per frame, had made the suite 60% slower on their own.

Seven other queries never got the same treatment. `frontLineX` is the worst —
the camera, both minimap markers and the enemy cannon all ask it every frame,
and it was walking the scenery to answer. `countUnitsOfKind` runs three times
a frame.

Moving those two:

    lanebattle_tests   6.10s -> 3.84s        (-37%)

`fight` and `removeTheDead` were left alone **deliberately**, and the reason is
worth more than the milliseconds. `entities()` is a vector in creation order;
a component pool is a hash map whose order is a standard-library detail. Order
does not matter to a count or to a coordinate, which is why the two queries
above could move — but it decides who swings first when two blows land in the
same frame, and therefore which of two units dies. Taking the faster pool
there would buy a fraction of a percent of one frame and pay for it with a
battle that can resolve differently on Linux than on Windows.

`engine_bench` still says the spatial grid is not owed: at the ~140 entities
this game peaks at, the scan column costs about 0.03 ms.

### The stage list would have drawn over its own instructions

Third time for this exact bug. `kMaxStages` is 24 and a data file may supply
that many; eight rows fit above the hint text. A ninth stage draws across
"CLICK A BATTLE, OR ENTER FOR THE LATEST", a tenth across "Q TO QUIT", and
everything past that off the bottom of the window where it can be neither seen
nor clicked — while `stageAt` cheerfully returned an index for rows nobody
could see.

The shipped campaign has exactly eight. One more and it is visible.

`kMaxVisibleStages` is derived from the hint position now, the same way
`kMaxVisibleButtons` is derived from the hero button. The pattern in all three
is identical: **two constants that had to agree, written down separately.**

### `integer()` cast a float to an int without checking the range

Engine-level, and it affects every integer any data file has ever supplied.
`stages_unlocked = 1e20` in a save file went through `static_cast<int>` — which
is undefined behaviour for a value out of range, not a large number — and was
clamped by its caller one step too late to matter. Clamped before the cast now,
with NaN falling through to the fallback.

Two smaller ones of the same shape were fixed in the game: the composition
parser and the save loader both accumulated digits into an `int` that a long
enough run would overflow on the way to being rejected.

### The frame limiter fought vsync above 60Hz

`Engine::run` slept to hit 60fps whether or not vsync was already doing it.
Present returns after one refresh — 6.9 ms at 144 Hz — the sleep adds 9.7 ms on
top, and the next present then waits for the following refresh boundary at
20.8 ms. **A 144 Hz monitor ran the game at 48 fps with uneven frame times.**

A 60 Hz display cannot show this: present already costs a full 16.6 ms there,
so the sleep computes to zero. The bug was exactly invisible on the machine
most likely to be testing for it, which is the second bug in this pass with
that property — `mutate.bat` was the other.

The renderer is asked once whether it actually got vsync, since the request can
be granted, refused, or dropped by the fallback, and the limiter follows what
happened rather than what was asked for.

### Noted, not fixed

- **Float parsing is locale-dependent.** `strtof` and `std::to_string` both
  follow the C locale, so on a machine using a comma decimal separator every
  fractional number in `units.txt` would silently truncate — `enemy_income =
  0.40` becoming `0.0`, which is not a fallback and would not look like a
  parse failure. Nothing in this project calls `setlocale`, so the locale is
  "C" and this cannot currently happen. It is one dependency away from being
  able to.
- **The title screen teaches three units.** It still reads `1 RUNNER 2 SOLDIER
  3 ARCHER` and says nothing about the griffin, the hero, the spells or the
  cannon. Not a bug — a content decision about what a new player should be
  told, and worth making deliberately.
- **Nothing is culled.** Every sprite, polygon and text is drawn every frame
  whether or not it is on screen. SDL clips, so it is correct, and the
  measured frame cost says it is not worth an early-out yet.

## Slice 5b, ten slices late and on purpose

The oldest open row on the roadmap, deferred since slice 5 with the note *"owed
the moment there are sprites"*. It was built when art was **committed to**
rather than when it arrived, which is a slightly different trigger and worth
being honest about: there is still no artwork in this game.

### What the engine gained — exactly what the row predicted

**`Animation`** — a component plus a system advancing `Sprite.srcX`, run from
`RunBuiltinSystems` so a game gets it without asking. It writes `srcX` and
nothing else, deliberately: frames run left to right along one row, and *which*
row — walking, attacking, dying — is the game's decision, made by setting
`srcY`. A component that also picked the animation would have to know what
animations mean.

Two details that are not decoration. The time-banking loop is a `while` rather
than an `if`, for the reason `TickTimer` documents — a frame that ran long owes
more than one step, and dropping the remainder makes every animation quietly
run slow exactly when the machine is busy. And a one-shot holds its last frame
and clears `playing`, so a death leaves a corpse in its final pose and the game
can *ask* whether an attack finished rather than timing it independently.

**`Sprite.flipX`** — the cheapest field in the engine, and it halves how much
art a game needs. `SDL_FLIP_NONE` was hardcoded for four games, so a unit that
faces both ways needed a second hand-mirrored copy of every frame. The
reference ships 2,161 unit frames; "twice as many" is not a rounding error.

### What the game gained, which is the part that matters

A roster row in `units.txt` can name a sheet:

    [unit]
    name         = SOLDIER
    sheet        = lanebattle/soldier.png
    frame_width  = 32
    frame_height = 48
    frame_count  = 6
    frame_seconds = 0.09

That is the entire art pipeline. Naming a sheet swaps the coloured block for
animated frames, drops the team tint (which would otherwise wash the artwork),
sets `flipX` from the unit's side, and suppresses the stick figure that was
standing in for art all along.

Building only the engine half would have been the speculative work this project
keeps declining — a frame animator nothing can reach. The rule is that the game
pulls features out of the engine, so the game pulls this one.

**With no texture cache it all falls back**, which is every test and both
simulators: they run with no window, and a unit there is still a block with a
figure. That is what keeps 700 assertions and 800 simulated battles running
headlessly now that art is possible.

### The preview pane, in forty lines

The real gap a hand-rolled engine has for art work is not the animator — it is
that you cannot *look* at an animation without first wiring it into the game. A
mature engine gives you a preview pane.

    ui_shots --sheet lanebattle/soldier.png 32 48 6

Renders every frame side by side, with the mirrored row underneath, into a PNG.
It answers the two questions a delivered sheet raises — are my frames sliced
where I think they are, and does mirroring look right — both of which are
geometry, invisible until drawn, and otherwise found after an artist has drawn
thirty of them.

### How it was verified, given there is nothing to animate

Three levels, because "the field exists" proves nothing:

- **Logic** (`engine_tests`, +17 checks): frames advance, loops wrap, one-shots
  hold and stop, a long frame pays out every interval it contained, and the
  numbers a data file could supply — a zero interval, a single frame, an
  Animation with no Sprite — are guarded rather than trusted.
- **Pixels** (`render_tests`, +9 checks): the test *writes a two-frame PNG*,
  loads it back through `TextureCache`, and asserts that flipping moves the
  red half from left to right and that advancing a frame changes the colour
  reaching the screen. The renderer ignored `SDL_FLIP_NONE` for four games; a
  field test would have kept ignoring it silently.
- **Data path** (`lanebattle_tests`, +11 checks): a sheet named in a roster
  file survives the load, changes none of the unit's balance numbers, and
  degrades to a block when there is no texture cache.

Writing the PNG caught a real mistake immediately: the test first handed
`TextureCache::load` an absolute path, and the cache resolves against the
executable, so it prefixed the base directory twice and found nothing. Loading
*through* the cache rather than around it is what met that rule — a test that
built a texture some other way would have proved the flip worked and said
nothing about whether a delivered sheet does.

## The cannon, and the economy

Two systems the probe had measured as dead: the cannon fired **zero shots** in
every column on every stage, and the INCOME upgrade could not change an
outcome. Both are fixed. Neither fix was the one that looked obvious.

### The cannon: the price was right, the reach was wrong

The obvious rebuild was the genre's shape — free, cooldown-limited, aimed,
reaching the fight. Free was measured twice and is wrong for this game:

    free, 760 reach     slice 8's stalemate exactly. Draws everywhere,
                        including the last stage drawing for the only player
                        who could previously win it.
    free, whole field   WORSE. Almost every column drawing on almost every
                        stage.

The second run is the one that taught something, because it killed the theory
behind the first. The stalemate is not geometry — the hypothesis was that a
partial arc draws an uncrossable line on the map, and covering the whole field
should therefore have removed it. It did not. **Two guns firing forever erase
both armies faster than either side can accumulate one.** Nobody gets ahead, so
nobody pushes, so no castle falls.

The 30-gold price was what stopped that all along. What was broken was a reach
of 420 from a castle at x=100 on a 2400-wide field: the only enemy it could
ever hit was one that had already crossed four fifths of the map.

Reach fixed to 1000 and the gun fired — and the GUNS column got **worse**, four
stages down to two, having fired 89 shells on one of them. Which is its own
finding: at 30 gold a shell buys about 50 damage, while 60 gold buys a soldier
worth ten times that over its life. Ninety shells is forty soldiers of gold
spent on four soldiers of effect.

    cost 30, reach 420      0 shots, GUNS 4 (= MIXED)
    cost 30, reach 1000     fires, GUNS 2 (worse than MIXED)
    cost 12, reach 1000     fires, GUNS 5 (> MIXED), wins stage 6 alone

Twelve gold every 3.2 seconds is about a quarter of base income — a tax you can
feel and can choose to stop paying. That is the rhythm "free with a cooldown"
was reaching for, at a price this economy can survive.

**The probe was wrong too, twice.** Its cannon block ran after the unit-spending
loop and reserved 120 gold on top of the shell, so the purse never reached the
threshold — the identical bug the ECON column had, in the same place. And it
aimed at the ENEMY front line, which is frequently off screen because the camera
follows yours, and a click outside the window is not a click.

### The economy: it cannot live inside a battle

Five configurations, every one inert or harmful:

    120 for +4, bought cautiously      4 stages (= MIXED)
    120 for +4, bought greedily        3 stages
     80 for +5, bought cautiously      4 stages
     80 for +5, bought greedily        2 stages
     45 for +5, two levels, early      2 stages

No price fixes it, because the problem is not the price. Gold spent in a battle
is gold not spent on the opening army; the opening army decides the line; and
kill rewards mean a lost line compounds into a lost battle. An in-battle economy
upgrade is a bet against the mechanic the whole design rests on.

Cheapening it also **buffed the opponent**, which was the sharpest lesson in the
sequence. The AI buys upgrades out of true surplus without ever risking the line
it is holding, so a cheaper INCOME hands it an economy the player still cannot
safely take — and the naive on-ramp stage stopped being winnable at all. INCOME
is back to 120 for +4, unchanged, deliberately.

The economy that works is **GRANARY**, a fifth permanent perk: +2 gold/second,
bought from the campaign bank between battles, out of money that could never
have been soldiers in the fight it affects. Same idea, moved somewhere it is not
self-defeating.

    ECON  4 (= MIXED)      the in-battle upgrade, played as well as it can be
    GRAIN 5                two levels of GRANARY, same army

## The loadout, and training

Also not roadmap slices, and the same reason: the roadmap was read off the
reference game's asset structure, which shows units and animation but not
"you own more than you can bring".

### The roster grew

A loadout is meaningless with four unit types and four slots. Three more roles,
each answerable — the rule the griffin set, because a unit nothing can counter
is the hero bug in a cheaper costume:

    PIKEMAN    the budget answer to the sky. Reaches air at melee range, with a
               soldier's build and a shorter purse.
    OGRE       the wall. Four soldiers of health, slow enough to arrive after
               the fight starts, and ground only — a griffin walks over it.
    BALLISTA   the longest reach in the game behind the softest body. Useless
               without a line in front, which is the archer's lesson taken
               further.

### You own everything and carry four

`kindForButton` used to mean "the nth sellable row", so ROSTER ORDER decided
what a player could field and anything past the bar's width was payable for and
unsendable. It means "what is in slot n of your loadout" now, and every route
into the field — the number keys, the bar, the title screen — asks that one
question.

Four slots against seven kinds, deliberately. Five would be a mild preference;
four is a decision you can get wrong. A `static_assert` ties the loadout to what
the bar can draw, because this file has now had five bugs from two copies of one
fact disagreeing and the compiler can check this one.

### Training

Every unit type levels, five deep, bought from the same bank the armoury and the
hero spend. Cost scales with what the unit costs to field, so training an OGRE
is a real investment and the cheap units stay cheap to improve — a flat price
would have made the expensive rows strictly the better thing to buy.

Player units only. The opponent fields the roster as written, for the same
reason WEAPONS is one-sided: training both sides arms both equally and buys
nothing.

Both halves live on one screen, reached with A. Choosing what to bring and
choosing what to invest in are the same question asked twice — there is no
point training an OGRE you never carry — and separate screens would let a
player answer one without seeing the other.

### An empty slot is a choice

The fallback to roster order applied PER SLOT, so carrying three units and
leaving the fourth blank produced a fourth button selling whatever roster order
put there: the army screen saying CARRYING 3 OF 4 while the spawn bar sold
four. Two screens disagreeing about one fact, which is this file's oldest
recurring bug and the fifth instance of it.

The fallback is all-or-nothing now — only "nobody has chosen anything" falls
back, which is what keeps every test written before loadouts existed meaning
what it meant then.

### Giving the new units a question to answer

The first measurement was bad: `MIXED 4, OGRE 5, PIKE 3, BALL 2`. Two of the
three new units were weaker than the army they replaced, and **two rounds of
buffing them moved nothing at all** — the outcome is a step function and
neither crossed a threshold, though battle times moved (PIKE 123s, BALL 130s
against MIXED's 61s), so they were being played rather than silently absent.

The diagnosis was not that the numbers were wrong. It was that **the campaign
never asked their questions.** `--sweep` said so plainly:

    against "1,3,1"     one griffin in three - every strategy within 0.10 of
                        every other. One archer already answers one flyer.
    against "1,3,3,1"   two griffins in four - every ground answer collapses to
                        0.50 income and the PIKEMAN sits at 1.30.

So a stage has to be air-HEAVY, not merely contain a flyer. **THE EYRIE**
replaces the old stage five: `1,3,3,1` at 0.65 income.

The income DROPS there, which looks wrong beside a table that otherwise climbs
and is not. Composition is far the stronger dial, and the sweep also set the
ceiling: WARDEN and CHAPLAIN cap at 0.70 against that composition, so holding
income up as well would have left the FALCONER the only path able to finish the
campaign — the exact failure the previous retune had just fixed.

### The ballista had no niche, so it got a different one

Buffing it never worked. At 48 damage it beat 0.80 income on the ground; at 72
— a fifty per cent buff — it beat 1.00, still under MIXED's 1.30, and moved
**not at all** against archers. Three passes, no niche.

The problem was structural. At 120 gold on a 14/s income you cannot field them,
and more importantly the ballista was competing with the ARCHER: both ranged
support, both hitting air, so it could only be a worse archer or a replacement
for one.

The fix was to take something away. It is **ground only** now, cheaper and
softer, and the difference is not subtle:

                        MIXED   BALLISTA
    ground "1,1,0"       1.30     1.30
    archers "1,1,2"      0.60     1.20
    archer-heavy         0.70     1.50

Outranging enemy archers is a real job that nothing else in the roster does.
The archer answers the sky; the ballista answers the ground line. Two ranged
units with no overlap, and the player picks by what the stage fields.

### The result

    stage            asks for              answered by
    5  THE EYRIE     an air wing           PIKEMAN
    6  BLACK FIELD   enemy archers         BALLISTA
    7  THE GATES     a ground grind        OGRE (and AIR, and BALL)
    8  THE KEEP      all of it             FULL only

    NAIVE 1  MIXED 4  AIR 5  ECON 4  GUNS 4  OGRE 5  PIKE 4  BALL 6  COMBO 4
    WARDN 7  FALCN 7  CHAPL 7  FULL 8

Every new unit is now at or above the army it replaced, each has a stage only
it answers, and the hero paths are still 7/7/7 — the retune did not cost the
balance the previous one bought.

### Specialising beats carrying a bit of everything

The COMBO column exists to check that four slots are not decoration: a player
carrying the anti-air AND the siege piece, against players carrying one of
each. It wins **4**, against BALL's 6 and PIKE's 4 — carrying both is worse
than carrying either.

That is not a bug, and it survived being given a proper front line rather than
a thin one. The limit on an army here is GOLD, not cooldowns, so adding unit
types does not add throughput — it splits the same purse across more expensive
units and thins the line everything else depends on.

So the loadout's answer is **re-equip for the stage you are about to fight**,
not "bring a balanced kit". Which is what the reference game does between
levels, and it is the reason to have a screen for it at all.

## The hero tree

Not a roadmap slice. The roadmap was built by reading the reference game's
asset structure, which shows units and animation but does not shout "the hero
is a build you commit to" — so this, the loadout and troop levelling were all
missing from the plan.

### The problem

Role compression. The hero tanked, out-damaged every unit AND reached the sky,
and `campaign_probe` measured what that meant: it beat every composition at
every income the sweep could build. A unit that answers everything is not a
decision, it is a substitute for playing well.

### The mechanic

Opportunity cost, and one exclusion that does the real work: **reaching the sky
is a path, not a birthright.** Two of the three paths cannot touch a griffin.

    WARDEN     HP 145%  DMG  85%  RATE 100%   ground only
    FALCONER   HP  70%  DMG 125%  RATE 118%   reaches the sky
    CHAPLAIN   HP 110%  DMG  60%  RATE  87%   ground only, mends 7/s

The hero's roster row is ground-only now. It used to reach the sky "because a
champion that loses to a bird is not much of one", and that one line was what
made the hero unanswerable. A baseline that already had it would also have made
specialising a *downgrade*, and a choice that costs you something you already
had is not a choice anybody makes twice.

A path is chosen once and kept, for the same reason the hero is one summon per
battle rather than an ability on a cooldown: a choice you can take back is not
a choice. Three upgrades per path, each capped at three levels, costs rising
geometrically — 27 combinations and a bank that never quite covers all of it.

### The fairness criterion, and why half of it was wrong

The criterion set before building was: no path may win more stages than the
others, **and each must win at least one stage the other two lose.**

The first run failed it — FALCONER 7, WARDEN 6, CHAPLAIN 6 — because stages 7
and 8 both fielded griffins, so anti-air was mandatory twice over and only one
path could finish the campaign. Stage 7 is a pure-ground grind now, and the
paths measure 7 / 7 / 7.

The second half of the criterion is **wrong for a permanent choice**, and
measuring is what made that obvious. If a stage can only be won by one path,
and the path is chosen once and kept, then two thirds of players meet a wall
they cannot ever pass. Uniqueness and irreversibility do not belong in the same
system.

So the differentiation has to be in HOW a path wins rather than WHETHER, and
that is measurable too — the same stage, by path:

    stage 7   WARDEN 104s   FALCONER 90s   CHAPLAIN 167s

Same destination, three routes, the chaplain taking nearly twice as long
because it wins by attrition rather than damage. That is the shape a permanent
choice should have.

### A crash it uncovered

The chaplain's aura test segfaulted, and the cause was older than the tree:
`removeTheDead` walked `world.entities()` and called `spawnShards` from inside
that loop. Shards are six new entities, each pushing onto the very vector being
iterated — and `ECS.h` says in as many words that `entities()` is the live list
and creating an entity while looping it can reallocate under the loop.

It had been there since slice 1 and only bit when a push happened to cross a
capacity boundary during a death. `fight` had gathered first since the
beginning for exactly this reason; this was the one place that had not.

The test binary also printed *nothing* when it died — six hundred checks and
not even the banner, because stdout was buffered and the dying process never
flushed. `main` sets it unbuffered now, which is how the next crash gets
located in one run instead of six.

## The UI pass, and the tool that made one possible

The stage-list bug below was found by a human starting the game. That is not a
method — it is luck, and it only works for whichever screen someone happens to
open. So the next thing built was `tests/ui_shots.cpp`: it drives the game to
twelve different screens and writes a PNG of each, using the same dummy-driver
trick `render_tests` uses, in about a second with no display.

It **cannot fail**. It makes pictures and a person has to look at them. That is
the point: the bug class it exists for is precisely the one no assertion
catches. The first run found five things, four of which had shipped for months.

### The font had no `+` and no `%`

Every permanent upgrade describes itself as `+10% DAMAGE`. The player saw a
hollow box, `10`, another hollow box, `DAMAGE`. All four armoury rows, unreadable.

The fallback worked exactly as designed — a missing character draws as a box so
it cannot vanish silently — and it still survived four slices, because nobody
had ever looked at the screen it was shouting on.

### The armoury text hung out of its plate

`+40 START GOLD  160` is nineteen characters, 226 pixels at that scale, inside
a 220-pixel plate. TREASURY's price sat out over the gap toward the stage list,
and one more digit would have put it underneath the list. The cost is
right-aligned on the name line now, which fixes every row and keeps working
when the geometric cost curve reaches four figures.

### The enemy castle was behind the spell panel

At the far-right camera position — exactly where you look when you are
attacking their gate — the enemy castle slid under the spell rows and lost the
top quarter of itself behind three buttons.

Nothing could have caught this. The castle was drawn correctly, at the right
place, in the right layer, with a panel on top of it, which is what panels are
for. The right-hand column is now positioned **from `kCastleTopY`** rather than
from a number that looked fine on an empty field, and the upgrade panel and
mana bar stack upward from it, so moving one cannot land on another.

### The mana bar was an unnamed blue strip

Pressed against the bottom of the upgrade panel, it read as a progress bar
belonging to SUPPLY. Three spells were priced in a resource the screen never
named. It has a gap and a `MANA 62` label now.

### The foreground parallax band was behind the spawn bar

The grass sat at `kWindowHeight - 6`, and the spawn bar covers everything from
480 down. So the one layer that moves *faster* than the ground — the half of
depth a boolean could never express, and the entire reason slice 6 pulled
`parallax` out of the engine — was drawn into a sliver nobody could see. It was
also 30/38/44, within a few points of the ground behind it, so even once moved
it still would not have read.

Two smaller things went with it: the hill bands were near-black on near-black,
because `Polygon` **strokes** rather than fills and there is no filled-polygon
call in this engine, so a hill is a triangle outline and needs the contrast to
show; and the castles were plain rectangles, now given battlements and a gate
out of `Polygon` — the same line-art trick, and the same reason, as the stick
figures over the units.

One thing this pass got wrong on the way: I added a screen-space ground band
believing there wasn't one, having misread a screenshot. `buildField` has drawn
a world-spanning ground bar since slice 1. Removed.

## The first bug found by looking at the game

Somebody started stage one and the stage list was still there — "CHOOSE A
BATTLE" across the middle of the battlefield, eight stage rows drawn through
the HUD, the armoury sitting on top of the spawn bar.

`SceneStack::push` does not call onExit on the scene underneath; only pop and
replace do. And the renderer draws **components, not scenes** — a frozen scene
still owns every entity it created, and the world is what gets drawn. That is
deliberate and right for the pause and game-over overlays, which want the
battle visible behind them. It is wrong for a full-screen menu, and only the
menu knows which of the two it is. `StageSelectScene` now clears its own
picture before pushing the battle; `onResume` already rebuilt everything from
scratch, so there was nothing to preserve.

Push is still correct here. The battle has to come back to this screen, which
is what pop and onResume do — replace would throw it away.

**Not one of the 600 checks in this file could see it.** They read game state,
and the state was perfect: the stage list was in the world, which is exactly
where it belongs while that scene is alive. `render_tests` could not see it
either — it proves a sprite lands where the camera says, and every one of
those sprites did.

The number was there the whole time, though.
`testTheHudStillIgnoresTheCameraEntirely` puts its `check` inside a loop over
every screen-space sprite, so its contribution to the total is a count of
them. Fixing this dropped the suite from 612 checks to 600: eight stage plates
and four armoury plates, the exact twelve that had been drawing over the
battle. A test had been quietly reporting the bug as an integer for weeks and
nobody was reading it as one.

Worth keeping in mind next time a check count moves for no reason.

## The campaign re-tune, and the instrument it needed

### What was wrong

    stage | plain | +hero
      1-7 |  WIN  |  WIN
        8 | LOSS  |  WIN

Seven of eight stages fell to one fixed army — soldier, soldier, archer — with
no adaptation at all, and the hero cleared the eighth on its own. The stage
table varies composition precisely so that stages ask different questions, and
measuring said they did not.

### The instrument

`tests/campaign_probe.cpp`, built for this and kept. Not a test: it prints a
table rather than passing, and is not registered with ctest, exactly like
`engine_bench`. It plays every stage seven ways —

    NAIVE  soldiers only              MIXED  soldier/soldier/archer
    AIR    mixed plus griffins        ECON   mixed, buying INCOME
    GUNS   mixed, using the cannon    HERO   mixed plus the one summon
    FULL   all of it

— and `--sweep` finds the highest enemy income each of them still beats, per
composition. That sweep is what made the retune possible: it turns "make stage
six a bit harder" into "put stage six between AIR and HERO."

**Three of its columns were silently measuring nothing**, and each looked like
a finding about the game rather than a bug in the probe:

- **ECON** matched MIXED to the second, because the unit cycle spent the purse
  before the upgrade check ever saw enough to buy one.
- **GUNS** matched MIXED to the second, because a click re-issued every frame
  never *releases* — and the cannon fires on the release. It fired nothing.
- **The whole sweep** reported every player beating every income, because
  `play()` reloaded the shipped roster on entry and threw away the scratch
  stage the sweep had just written.

Each produced a believable table of numbers meaning nothing. The probe now
prints the stage table and hero stats it actually loaded before measuring
anything, and counts shots fired and upgrades bought — the same guard
`mutate.bat` uses when it checks its mutation applied.

That guard earned itself immediately: assets are **copied next to the binary at
build time**, so editing `units.txt` and re-running without rebuilding measures
the previous table. That cost three rounds of "the change had no effect,"
including a hero retune that never reached the binary.

### What the measurement found

- **Composition is far the stronger dial.** Giving the enemy archers drops what
  a ground army survives from about 1.4 income to about 0.6 — a bigger swing
  than the campaign's entire income range.
- **The old hero was a win button.** At 620 health and 46 damage it beat every
  composition the probe could build at every income it could reach. Retuned to
  360 and 32: still worth about three soldiers of health and three of damage,
  free and instant, but no longer a campaign you cannot lose.
- **The cannon never fires.** Zero shots across every run of every strategy.
  Its range is 420 from your own castle, so it only reaches an enemy that has
  already arrived at your gate — and this game's collapses are unrecoverable.
  Slice 8 is, as balanced, unreachable content.
- **INCOME cannot decide a stage.** Bought greedily it is worse than not buying
  at all; bought only while winning it arrives after the outcome is settled.
- **Outcomes are a step function, and not a monotonic one.** A stage is held
  untouched or lost outright with almost nothing between, and a stronger player
  can lose a stage a weaker one wins. This is why the table cannot be tuned by
  reading it, and why the sweep's numbers are lower bounds rather than true
  thresholds.

### The result

    stage  asks for                    NAIVE MIXED AIR  ECON GUNS HERO FULL
    1      nothing - learn the button   WIN   WIN  WIN   WIN  WIN  WIN  WIN
    2-4    a mixed army                draw   WIN  WIN   WIN  WIN  WIN  WIN
    5      the sky                     draw  draw  WIN  draw draw  WIN  WIN
    6-7    the hero                    loss  loss loss  loss loss  WIN  WIN
    8      all of it                   loss  loss loss  loss loss loss  WIN

    stages won                            1     4    5     4    4    7    8

Every column's wins turn into losses, and the turn comes later for the players
who know more. Stages 2-5 escalate on income against one plain ground army;
6-8 hold income roughly still and escalate on composition instead, because
that is the dial the measurement says is strong.

### What it cost the tests

Three design guards encoded the old curve and had to move — and one of them was
wrong in a way worth recording. `testTheLastStageNeedsMoreThanComposition`
**gave** the player two INCOME upgrades and checked they won. Handed 318 gold
of upgrades for free, they did. Paid for, the probe says they change nothing at
all. The test passed on a fiction and asserted the capstone was gated on a
system that cannot gate anything.

The mono-type guard moved from stage 5 to stage 6 for a smaller reason with the
same shape: on the new curve a mono-type army *stalemates* stages 2-5 rather
than losing them. "Did not win" and "lost" are different facts, and a test
saying `== -1` should mean the second — so the assertion moved to where it is
strictly true instead of being loosened to fit where it already pointed.

### Still open

`testTheShippedTableMatchesTheCompiledDefaults` now guards the stage table and
the hero against the file and the header drifting apart, since retuning meant
editing both by hand — the fourth instance in this file of two copies of one
fact, after the hero button, the number keys and the stage list.

The cannon and the economy are still inert. Both are balance problems with
real fixes — a longer cannon reach, an income upgrade that is cheap enough to
buy early — and both are their own piece of work rather than part of this one.

## The campaign was flat, and the hero flattened what was left

The re-tune flagged after slice 11 now has numbers. Playing every stage with
one fixed army — soldier, soldier, archer, no cannon, no spells, no upgrades —
and then the same army with the hero summoned:

    stage | plain | +hero
    ------+-------+------
        1 |  WIN  |  WIN
        2 |  WIN  |  WIN
        3 |  WIN  |  WIN
        4 |  WIN  |  WIN
        5 |  WIN  |  WIN
        6 |  WIN  |  WIN
        7 |  WIN  |  WIN
        8 | LOSS  |  WIN

Two things, and the second is the bigger one.

**The hero wins the only stage that was asking a question.** Stage 8 is the
one gate in the campaign, `testTheLastStageNeedsMoreThanComposition` exists to
keep it that way, and a free one-per-battle summon clears it with nothing else
changed. That guard still passes because it plays without the hero.

**Seven stages fall to the same army with no adaptation.** The stage table
varies composition specifically so that stages ask different questions, and
measuring says they currently do not. The curve is flat and then a cliff, and
the hero has now taken the cliff out.

That is a re-tune of the stage table, not a bug fix, and it is the next real
piece of work.

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
