#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# run.sh — build the project and play a game. The Linux/macOS twin of run.bat.
#
# Configures with CMake the first time, builds Release every time, then runs
# whichever game you pick. Safe to run repeatedly: the build is incremental.
#
#     ./run.sh              ask which game
#     ./run.sh asteroids
#     ./run.sh breakout
#     ./run.sh tests
#     ./run.sh clean        throw the build folder away and stop
#
# First time only:  chmod +x run.sh
#
# To make it double-clickable: on macOS, copy it to run.command (Finder runs
# .command files in Terminal). On Linux, most file managers offer "Run in
# Terminal", though some need it enabled in their preferences first.
# ----------------------------------------------------------------------------

set -euo pipefail

# Work from the folder this script lives in, not wherever it was launched.
cd "$(dirname "$0")"

game="${1:-}"

if [ "$game" = "clean" ]; then
    echo "Removing the build folder..."
    rm -rf build
    echo "Done."
    exit 0
fi

# --- Configure (first run only) ---------------------------------------------
# SDL2 comes from the system package manager here, which CMake finds through
# pkg-config. See README.md if it reports SDL2 missing.
if [ ! -f build/CMakeCache.txt ]; then
    echo "Configuring..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi

echo "Building..."
cmake --build build --config Release

# --- Ask which game ---------------------------------------------------------
if [ -z "$game" ]; then
    echo
    echo "  [1] Asteroids"
    echo "  [2] Breakout"
    echo "  [3] Run the tests"
    echo
    while [ -z "$game" ]; do
        read -r -p "Choose 1-3: " choice
        case "$choice" in
            1) game="asteroids" ;;
            2) game="breakout" ;;
            3) game="tests" ;;
        esac
    done
fi

# --- Run --------------------------------------------------------------------
if [ "$game" = "tests" ]; then
    ctest --test-dir build --output-on-failure
    exit 0
fi

# Single-config generators (Make, Ninja) put the binary straight in build/;
# multi-config ones put it in build/Release/. Accept either.
for candidate in "build/$game" "build/Release/$game"; do
    if [ -x "$candidate" ]; then
        echo
        echo "Starting $game - Escape quits."
        exec "$candidate"
    fi
done

echo "Could not find an executable for '$game'." >&2
echo "Expected one of: asteroids, breakout, tests" >&2
exit 1
