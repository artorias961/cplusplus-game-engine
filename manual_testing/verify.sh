#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# verify.sh — the checks this project actually uses, in one place.
#
# run.sh plays the games. This runs the things you do before believing a change
# is finished. Every option here is something that has caught a real bug in
# this repository at least once; none of it is ceremony.
#
#     ./verify.sh            ask which check
#     ./verify.sh quick      build, then every test once
#     ./verify.sh flake      the same tests twenty times over
#     ./verify.sh smoke      do the shipped games actually start?
#     ./verify.sh render     the pixel tests, verbose
#     ./verify.sh bench      how much do the naive parts cost?
#     ./verify.sh clean      throw the build away and rebuild from nothing
#     ./verify.sh archive    do the frozen versions still build on their own?
#     ./verify.sh all        everything. Run this before you commit.
#
# See mutate.sh for the other half of the story: these check that the code
# passes its tests, that one checks the tests would notice if it didn't.
# ----------------------------------------------------------------------------

set -uo pipefail

# The project root is the parent of this script's folder, whatever the shell's
# working directory happens to be.
cd "$(dirname "$0")/.." || exit 1

CHECK="${1:-}"
FAILED=0

heading() {
    echo
    echo "=============================================================="
    echo "  $1"
    echo "=============================================================="
}

# Runs a command, remembers if it failed, and keeps going. A verification run
# that stops at the first problem tells you about one problem.
attempt() {
    local label="$1"
    shift
    if "$@"; then
        echo "  OK: $label"
    else
        echo "  FAILED: $label"
        FAILED=1
    fi
}

# Single-config generators (Make, Ninja) put binaries straight in build/;
# multi-config ones (Visual Studio, Xcode) put them in build/Release/.
binary_path() {
    if [ -x "build/$1" ]; then
        echo "build/$1"
    else
        echo "build/Release/$1"
    fi
}

configure_if_needed() {
    if [ ! -f build/CMakeCache.txt ]; then
        echo "Configuring..."
        cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || exit 1
    fi
}

build_it() {
    configure_if_needed
    echo "Building..."
    cmake --build build --config Release
}

# --- The checks -------------------------------------------------------------

do_quick() {
    heading "Every test, once"
    build_it || { FAILED=1; return; }
    attempt "all tests" ctest --test-dir build -C Release --output-on-failure
}

# Twenty runs, not one.
#
# The first real bug this repository's CI ever caught failed eleven times in
# two hundred: a test that killed the player's ship with a random rock and then
# dereferenced it. A single green run would have shipped that.
do_flake() {
    heading "Every test, twenty times (hunting for flakes)"
    build_it || { FAILED=1; return; }
    attempt "20x repeat" ctest --test-dir build -C Release --output-on-failure \
        --repeat until-fail:20 -LE smoke
}

# The shallowest test in the project, covering its deepest untested seam.
#
# Everything else drives the games through tests/Harness.h, which reimplements
# the loop. Nothing else executes main.cpp, the real Engine::run, window
# creation or audio startup — a game that crashed on its first frame would pass
# every other check here.
do_smoke() {
    heading "Do the shipped games start?"
    build_it || { FAILED=1; return; }
    attempt "games start" ctest --test-dir build -C Release --output-on-failure -L smoke
}

# The renderer, checked against pixels rather than by eye. SDL's dummy video
# driver means no window and no GPU is needed, so this runs anywhere.
do_render() {
    heading "The renderer, checked against real pixels"
    build_it || { FAILED=1; return; }
    export SDL_VIDEODRIVER=dummy
    attempt "render tests" "$(binary_path render_tests)"
    unset SDL_VIDEODRIVER
}

# Not pass/fail. Numbers, so "optimise it later" can be decided rather than
# argued about.
do_bench() {
    heading "What the naive parts cost"
    build_it || { FAILED=1; return; }
    "$(binary_path engine_bench)"
}

# Catches what an incremental build hides: a header nobody includes any more, a
# stale object file, a CMake change that only works because your build folder
# already had the answer.
do_clean() {
    heading "Rebuild from nothing"
    rm -rf build
    build_it || { FAILED=1; return; }
    attempt "all tests after a clean build" \
        ctest --test-dir build -C Release --output-on-failure
}

# The README promises each archived version still builds on its own. Nothing
# else checks that promise, and the archives are never updated — so the only
# way it can break is a change outside them, which is exactly the kind of
# breakage nobody would look for.
do_archive() {
    heading "Do the frozen versions still build?"
    for version in archive/*/; do
        [ -f "${version}CMakeLists.txt" ] || continue
        local name
        name="$(basename "$version")"

        echo
        echo "--- $name"
        if cmake -S "$version" -B "${version}_verify" -DCMAKE_BUILD_TYPE=Release \
            >/dev/null 2>&1 && \
           cmake --build "${version}_verify" --config Release >/dev/null 2>&1; then
            echo "  OK: $name builds"
        else
            echo "  FAILED: $name does not build"
            FAILED=1
        fi
    done
}

do_all() {
    do_clean
    do_flake
    do_smoke
    do_render
    do_archive
    do_bench

    heading "Verdict"
    if [ "$FAILED" -eq 0 ]; then
        echo "  Everything passed."
    else
        echo "  Something failed. Scroll up; the failures say OK or FAILED."
    fi
}

# --- Menu -------------------------------------------------------------------

if [ -z "$CHECK" ]; then
    echo
    echo "  [1] quick    every test, once"
    echo "  [2] flake    every test, twenty times"
    echo "  [3] smoke    do the shipped games start?"
    echo "  [4] render   the renderer, checked against pixels"
    echo "  [5] bench    what the naive parts cost"
    echo "  [6] clean    rebuild from nothing, then test"
    echo "  [7] archive  do the frozen versions still build?"
    echo "  [8] all      everything (run this before committing)"
    echo
    while [ -z "$CHECK" ]; do
        read -r -p "Choose 1-8: " choice
        case "$choice" in
            1) CHECK="quick" ;;
            2) CHECK="flake" ;;
            3) CHECK="smoke" ;;
            4) CHECK="render" ;;
            5) CHECK="bench" ;;
            6) CHECK="clean" ;;
            7) CHECK="archive" ;;
            8) CHECK="all" ;;
        esac
    done
fi

case "$CHECK" in
    quick)   do_quick ;;
    flake)   do_flake ;;
    smoke)   do_smoke ;;
    render)  do_render ;;
    bench)   do_bench ;;
    clean)   do_clean ;;
    archive) do_archive ;;
    all)     do_all ;;
    *)
        echo "Unknown check: $CHECK" >&2
        echo "Expected one of: quick flake smoke render bench clean archive all" >&2
        exit 1
        ;;
esac

exit "$FAILED"
