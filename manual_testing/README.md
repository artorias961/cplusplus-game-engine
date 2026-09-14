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
verify.bat linux      the GitHub Linux job, in Docker
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

### linux
The GitHub Linux job, run on this machine before you push. Needs Docker Desktop
running; the first run downloads Ubuntu and takes a few minutes, every run
after that takes about one.

Everything else here builds with **MSVC**. CI's Linux job builds with **GCC**,
and the two disagree more than you'd expect. The Linux job was red for three
commits in a row over one missing `#include <cstring>`: MSVC and macOS's libc++
both pull it in through `<string>`, GCC's libstdc++ doesn't, so `std::strcmp`
compiled everywhere it was ever tried. And a build failure **skips every later
step** — for those three commits Linux ran no tests at all, and the only way to
read why was to log in to GitHub.

`manual_testing/linux-ci.Dockerfile` is the same Ubuntu as the workflow's
`runs-on` and the same packages; `linux-ci.sh` is the same steps. It builds
with `make -k`, so one run shows *every* compile error rather than the first —
CI stops at the first, which is how a push that fixes one discovers the next.

It tests what `git add -A` would commit — tracked files plus new ones that
aren't ignored — and lists the new ones first. A file the build needs that git
isn't tracking yet shows up there, before it shows up as a red build.

It also prints GCC's warnings, which fail nothing but which you'd otherwise
never see. The one left at the time of writing, `-Wnonnull` inside
`stl_algobase.h`, is a known GCC 12/13 false positive in `std::vector`
assignment from a brace list. The other two it found were real: an unused
function, and a hero-upgrade array indexed without the range check its
neighbouring lookup applies.

In `all`, a machine without Docker running gets a loud **SKIPPED** in the
verdict rather than a failure — refusing to finish would just teach people to
stop running `all`.

**Keep the Dockerfile's `FROM` and the workflow's `runs-on` in step.** The
workflow pins `ubuntu-24.04` instead of `ubuntu-latest` for exactly this
reason: "latest" moves to a new Ubuntu and a new GCC on GitHub's schedule, and a
local check that has drifted from the runner is a check that passes for the
wrong reason.

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

Two more, from running a batch of sixteen mutations from PowerShell:

- **A caller with a deadline kills it mid-mutation.** A batch of mutations takes
  half an hour; the shell it was launched from gave up at ten minutes and took
  the script down between applying the first mutation and restoring it, leaving
  `const bool timeUp = false;` in the tree. Long batches go in a detached process
  (`Start-Process`), fed a newline for the `pause` at the end. If one is ever
  killed, the original is in `%TEMP%\mutate_backup_*.bak` — compare before you
  copy it back, so you know the mutated line is the only difference.
- **A find with no spaces in it can lose its quotes.** PowerShell quotes an
  argument for cmd only if it contains a space, and cmd then reads `<` and `>`
  as redirects: `static_cast<int>(std::ceil(seconds))` asked for input from a
  file called `int`. The script never started, so there was no verdict at all —
  one line, "The system cannot find the file specified", in the middle of a
  log of CAUGHTs. Pick a fragment with a space in it, which gets quoted.

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

It also, for a while, **wrote to the player's save**. Two of the shots are the
victory and defeat screens, staging a victory runs the real battle logic, and
the real battle logic banks the reward and saves — and this tool was the one
thing that never named a save path, so it got the default, which was the real
one. Taking screenshots paid gold into whoever's campaign was on the machine.
Nothing failed and nothing was logged.

The tool now names its own scratch file, and more to the point the default
changed: the player's save has to be asked for by name
(`lanebattle::usePlayerSavePath()`, called only by the game's `main`). Anything
that forgets writes `campaign-scratch.txt` beside itself. **A diagnostic tool
that can damage the thing it is diagnosing is worse than no tool.**

It photographs the game **as the player sees it**: it hands the game a texture
cache, so battles draw the environment paintings, the unit art and the effects,
and it seeds the presentation's dice so the random weather lands in the same
places every run. Until that line existed every battle screenshot showed the
no-art fallback — coloured blocks on procedural hills — which was a game nobody
played any more, photographed faithfully.

It also previews sprite sheets, which is the closest thing this engine has to
an animation editor:

```bash
./build/Release/ui_shots --sheet assets/lanebattle/lane-battle-gba-art/units/friendly/soldier.png 209 209 6 2
```

One row of frames side by side — the last argument is which row: 0 idle, 1
walk, 2 attack, 3 hurt, 4 stunned, 5 death — with the mirrored row underneath,
into `ui_shots/sheet-preview.png`. It answers the two questions a delivered
sheet raises — are the frames sliced where you think, and does mirroring look
right — before any of it is wired into a unit. The path resolves against the
binary, like every other asset, which is why it starts `assets/`. It scales
DOWN to fit; it used to crop, silently, which cut the last frames off every
1254-pixel generated sheet.

And it studies **motion**, which no single screenshot can show:

```bash
./build/Release/ui_shots --motion
```

Every unit walks in place, then stands, then swings, photographed twenty times
a second into `ui_shots/motion-study.png` — a row per unit, a column per moment,
with a line where the feet belong — plus a log of which pose and frame each one
showed, and a count of how often units in a real 20-second battle changed pose
and cut a walk short. It exists because the question "do the units walk
naturally?" got asked by a person watching the game, and the first answer had
to be a measurement: it showed every frame WAS being used, and that the runner's
cycle strobed at 26 frames a second, the bodies never bobbed, and the griffin
carried specks of its own sheet's row above. All three were fixed against it.

The defeat screens are **played, not staged**. Since the defeat screen explains
what the battle did, a staged battle — one enemy soldier set down at your gate
— gives it nothing to say. `12-defeat` is soldiers alone against THE EYRIE's
air wing, fought to the end; `13-battle-swarm-coming`, `14-the-swarm` and
`15-defeat-to-the-swarm` are the mixed army that holds THE GATES and never
breaks them, fast-forwarded to the red clock, into the swarm, and on to
whatever the swarm decides. Two findings came out of these before any number
did. The first played defeat said the player ended with 979 gold unspent and
never mentioned sending soldiers, because none had been sent — a hole in slot
one hid the soldier in slot two from its key (see `v3-plan.md`). And the first
swarm screenshot, five minutes into the swarm, showed the mixed army still
holding on an untouched castle behind a queue of thirty red soldiers: a swarm
that only got tougher was a queue, and needed to hit harder too.

## art_probe — is this art usable?

```bash
./build/Release/art_probe                                    # every sheet
./build/Release/art_probe <path> 6 6                         # one sheet
./build/Release/art_probe --art > assets/lanebattle/art.txt  # measure into art.txt
./build/Release/art_probe --show <path> 6 6                  # see the problems
```

The art is generated, and the generator does not do what it was asked: the unit
sheets were requested as 288-pixel grids and arrived as 1254-pixel ones, the
effects came back three times as wide as tall instead of six, and on the
environment paintings it twice drew a *picture* of transparency — a painted
checkerboard — instead of the real thing. So every image is measured before it
is used. For each sheet the probe prints:

- whether the background is **really transparent**, and how much faint **haze**
  there is — pixels nearly but not quite clear, invisible on the dark
  backgrounds every viewer uses and a grey smudge on a bright one;
- per row, where the content sits, where the **feet** are (the centre of the
  bottom of the figure, not of the whole picture — a raised sword moves the
  second and not the first), and how many pixels **bleed** onto the cell's
  border, which is a pose sliced in two;
- the numbers `art.txt` wants.

`--art` writes those numbers as `art.txt`, which is where the game reads every
sheet's grid and feet from. **Rerun it whenever an image changes**, and rebuild
— a sheet `art.txt` does not describe is not used, and `lanebattle_tests` fails
if `units.txt` or `effects.txt` names one that is missing. `--show` writes
`<name>-probe.png` beside the binary with haze in magenta and the grid in green,
which is how the halo round every generated unit was found.

It cannot fail, like the others. Its first run found that every unit sheet is
genuinely transparent and cleanly gridded in its idle and walk rows — and that
the attack rows run sword tips into the next frame's cell.

## campaign_probe — is the campaign asking anything?

The throwaway version of this (below) got run enough times that it stopped
being throwaway. It is now `tests/campaign_probe.cpp`, built like any other
target and, like `engine_bench`, deliberately **not** a ctest test: it prints a
table to read rather than passing or failing.

```bash
cmake --build build --config Release --target campaign_probe && ./build/Release/campaign_probe
```

- no arguments — every stage played fourteen ways: `WIN`, `loss`, or `swarm` (a
  loss after the ten-minute clock: the line held, never broke theirs, and the
  swarm broke it). It plays five minutes past the game's clock rather than
  imposing a cutoff of its own, so `draw` means the swarm failed to end a
  battle and should never appear
- `--detail` — plus seconds, both castles, shots fired, upgrades bought
- `--sweep` — the highest enemy income each player still beats, per enemy
  composition. This is the one that makes tuning possible: it turns "make
  stage six a bit harder" into "put stage six between AIR and HERO". It is also
  much the slowest, because it plays the whole income range for every strategy.
  `--sweep 1,3,3,1 4` sweeps one composition and wave size of your own instead
  of the three built in — which is how THE EYRIE's thresholds were checked
  before and after the queue freeze was fixed (0.50 to 0.60 for the armies with
  no anti-air, nothing else moved).

**When two instruments disagree, one of them is broken.** The sweep put MIXED's
ceiling at 0.60 income against composition `1,1,2`, while the stage table had
MIXED winning BLACK FIELD — the same composition at 1.15 — untouched, in 121
seconds. Both were reporting honestly about different things, and the sweep had
two faults behind one symptom:

- its scratch stage scaled castle health at `300 + 850x` against a shipped table
  that runs at roughly half that gradient, so it was measuring a castle 40%
  larger than any stage ships — and a bigger castle is a longer battle, which is
  time the enemy's economy gets to spend;
- it stopped scanning at the first income it failed to win, on the theory that
  outcomes are a step function. A **draw** is not a win, so one stalemate
  truncated the whole column — and the run is not monotonic anyway, because a
  richer enemy sends more units, more units die, and the bounty on them is the
  player's income too. A strategy that stalls against a poor opponent really can
  beat a rich one, and the sweep now prints `(but loses 0.90)` when it sees that
  rather than hiding it.

Every stage placed against that column was placed against a measurement that had
quit early.

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
