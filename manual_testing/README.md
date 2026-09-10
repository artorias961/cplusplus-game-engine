# manual_testing

`run.bat` / `run.sh` play the games. These run the checks you do before
believing a change is finished.

Nothing here is ceremony. Every check in `verify` has caught a real bug in this
repository at least once, and the notes below say which.

| | Windows | Linux / macOS |
| --- | --- | --- |
| The checks | `verify.bat` | `./verify.sh` |
| Break the code on purpose | `mutate.bat` | `./mutate.sh` |

On Linux and macOS, once: `chmod +x verify.sh mutate.sh`.

---

## verify — does it still work?

```
verify.bat            ask which check          ./verify.sh
verify.bat quick      every test, once         ./verify.sh quick
verify.bat flake      every test, 20 times     ./verify.sh flake
verify.bat smoke      do the games start?      ./verify.sh smoke
verify.bat render     the pixel tests          ./verify.sh render
verify.bat bench      what the slow bits cost  ./verify.sh bench
verify.bat clean      rebuild from nothing     ./verify.sh clean
verify.bat archive    do the frozen versions still build?
verify.bat all        everything — run this before committing
```

### quick
Build, then every test once. What you run while working.

### flake
The same tests **twenty times**. Not superstition: the first real bug this
project's CI ever caught failed **11 times in 200**. A test killed the player's
ship with a randomly-placed rock and then dereferenced it. One green run would
have shipped that happily.

Skips the `smoke` label, because starting a process is not the kind of test
repetition finds flakes in — and repeating it doubled the pipeline for no
information.

### smoke
Runs each **shipped executable** headlessly for half a second and requires it
to exit cleanly.

This is the shallowest test in the project and it covers its deepest untested
seam. Every game's *rules* are driven through `tests/Harness.h`, but the
harness reimplements the loop rather than using it — so `main.cpp`, the real
`Engine::run`, window creation, texture loading and audio startup were executed
by nothing at all. A game that crashed on its first frame would have passed
every other test in the suite.

Proved by deleting the software-renderer fallback in `Engine`: five test suites
stayed green, all three game binaries died.

### render
The renderer, checked against **actual pixels**. SDL's `dummy` video driver and
a software renderer mean no window and no GPU, but a real back buffer that can
be read back and asserted on.

For four games this was the hole in the middle of the suite: the one part that
decides what a player *sees* was checked by looking at it.

### bench
Not pass/fail — numbers, so "optimise it later" can be decided rather than
argued about. Read the row nearest your own entity count before believing
anything needs a spatial grid.

### clean
Wipes `build/` and starts over. Catches what an incremental build hides: a
header nobody includes any more, a stale object file, a CMake change that only
worked because your build folder already had the answer.

### archive
Each folder under `archive/` is a frozen, self-contained copy of the project
that the README promises still builds on its own. They are never updated, so
the only way that promise can break is a change made *outside* them — exactly
the kind of breakage nobody thinks to look for.

---

## mutate — would a test notice if it broke?

```
mutate.bat <file> <find> <replace> [ctest-name]
./mutate.sh <file> <find> <replace> [ctest-name]
```

```bash
./mutate.sh include/engine/View.h "worldX - camera.x" "worldX + camera.x" render_tests
```

It replaces the text (**literally** — paste C++ in without escaping anything),
rebuilds, runs the tests, and tells you:

- **CAUGHT** — good. The tests would notice this bug.
- **SURVIVED** — bad. Either the change genuinely doesn't matter, or you've
  found a test that doesn't test what it claims to.

The file is always restored, and the script rebuilds and re-checks before
telling you it's safe to walk away.

### Why this matters more than any of the above

A test that *exercises* a line is not a test that would *notice the line being
wrong*, and there is no way to tell them apart by reading. Every one of these
was written in good faith, ran the code it meant to run, and caught nothing:

- A cannon **range** test stood a unit at x=1600 and aimed at where it *stood*.
  The unit walks 80 pixels during the shell's flight, so it left the blast
  whether or not the cannon could reach. Setting the range **ten times too far**
  changed nothing the test looked at.
- A **font** test probed the first lit column of a glyph — column *zero*, where
  `col * scale` and `col` are both nought. A renderer that squashed every string
  to a third of its width passed cleanly.
- A **per-field fallback** was checked only for `range`, so breaking the
  fallback on `cost` left the whole suite green.
- A **determinism** test ran the same case six times and checked the answers
  agreed. They agreed with or without the rule it was testing.

Four tests, four false greens, all found by breaking the code on purpose.

### One thing this tool learned the hard way

Restoring the file is not enough. On Windows, `copy` gave the restored file an
*older* timestamp than the objects built from the mutated source, so MSBuild
decided there was nothing to do — leaving correct source and **mutated
binaries**, a state that looks fine and fails mysteriously. Both scripts now
bump the timestamp, rebuild, and confirm the suite is green before they finish.

---

## ui_shots — what does the screen actually look like?

The hole this fills was open from slice 4 to slice 12 and cost four real bugs.
Nothing in this project could see the SCREEN: `render_tests` asserts that one
sprite lands where the camera says, which is geometry, and `lanebattle_tests`
asserts about game state, which is arithmetic. Neither notices a panel drawn
over another panel, a label hanging off its plate, a character the font does
not have, or an entire menu still being drawn over the battlefield.

```bash
cmake --build build --config Release --target ui_shots && ./build/Release/ui_shots
```

Writes a PNG of every screen — title, both stage-select states, an empty
battle, a melee with flyers, the hero and an armed spell, a shot in flight,
the pause overlay, the camera at both castles, victory and defeat — into
`ui_shots/` next to the binary. Takes about a second, needs no display, and
runs on a machine with no graphics hardware, same dummy-driver trick as
`render_tests`.

**It cannot fail.** It makes pictures; a person has to look at them. That is
the whole point — the bug class it exists for is the one no assertion catches.
The first run of it found five things, four of which had shipped for months.

It also previews sprite sheets, which is the closest thing this engine has to
an animation editor:

```bash
./build/Release/ui_shots --sheet lanebattle/soldier.png 32 48 6
```

Every frame side by side, with the mirrored row underneath, into
`ui_shots/sheet-preview.png`. It answers the two questions a delivered sheet
raises — are the frames sliced where you think, and does mirroring look right —
before any of it is wired into a unit. The path resolves against the binary,
like every other asset.

## campaign_probe — is the campaign asking anything?

The throwaway version of this (below) got run enough times that it stopped
being throwaway. It is now `tests/campaign_probe.cpp`, built like any other
target and, like `engine_bench`, deliberately **not** a ctest test: it prints a
table to read rather than passing or failing.

```bash
cmake --build build --config Release --target campaign_probe && ./build/Release/campaign_probe
```

- no arguments — every stage played seven ways, win/loss per stage
- `--detail` — plus seconds, castle left, shots fired, upgrades bought
- `--sweep` — the highest enemy income each player still beats, per enemy
  composition. This is the one that makes tuning possible: it turns "make
  stage six a bit harder" into "put stage six between AIR and HERO".

**Rebuild before you believe it.** Assets are copied next to the binary at
build time, so editing `assets/lanebattle/units.txt` and re-running without
rebuilding measures the *previous* table and prints a perfectly plausible
result. That cost three rounds of "my change had no effect" during the retune.
The probe prints the table and hero stats it actually loaded before it measures
anything, for exactly this reason — read that header first.

It also counts shots fired and upgrades bought, because three of its columns
once measured nothing at all while looking like findings about the game: a
strategy that never bought an upgrade, one whose every-frame click never
released and so never fired, and a sweep whose scratch stage was overwritten
before it ran. A column that exactly matches its neighbour is the tell.

## Not scriptable, but worth knowing: balance probing

The technique the probe above grew out of, for anything it doesn't cover. Add a
temporary function to `tests/lanebattle_tests.cpp` that plays whole battles
under different strategies and prints the results:

```
PROBE| mixed, nothing else        WON  195s  800 vs -12
PROBE| all soldiers               LOST 212s   -4 vs 1580
PROBE| all archers                LOST  69s   -8 vs 1060
```

Run it, read it, delete it. Every serious design problem in this project was
found this way and by nothing else:

- the game was an unbreakable **stalemate** — both front lines frozen at the
  same two pixels for four minutes, castles untouched;
- it was later **unwinnable**, losing every one of eight strategies;
- the castle cannon made *defending* strictly better than attacking, so every
  match ended 800–800.

Every individual rule test passed the whole time. Assertions check that a rule
does what it says; only playing the game checks whether the rules add up to
one.
