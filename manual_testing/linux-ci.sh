#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# linux-ci.sh — the GitHub Linux job, run inside linux-ci.Dockerfile.
#
# Not meant to be run by hand. `verify.bat linux` starts the container, mounts
# the repository read-only at /repo and the list of files to test at
# /files.txt, and runs this.
#
# Why it exists: the Linux job was red for three commits running and nobody
# could see why without logging in to read the log. Every one of those commits
# built and passed on Windows, and on macOS in CI. The cause was one missing
# `#include <cstring>` — MSVC and macOS's libc++ both provide it through
# `<string>`, GCC's libstdc++ does not — and a build failure skips every later
# step, so for those three commits Linux ran no tests at all. A compiler you
# never run locally is a compiler you find out about after pushing.
# ---------------------------------------------------------------------------
set -u

# A copy, not the mount itself: the build writes into the tree, and the mount
# is read-only on purpose so nothing this does can touch your working copy.
tar -C /repo -cf - -T /files.txt | tar -C /src -xf -

status=0
step() { echo; echo "=============================================================="; echo "  $1"; echo "=============================================================="; }

step "Configure"
if ! cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > configure.log 2>&1; then
    cat configure.log
    echo "  FAILED: configure"
    exit 1
fi

# `-k`, so one run shows EVERY error rather than the first. CI stops at the
# first, which is how a push that fixes one error discovers the next.
step "Build (GCC $(gcc -dumpversion))"
cmake --build build --parallel "$(nproc)" -- -k > build.log 2>&1
build=$?
grep -E ": error|: fatal error|undefined reference" build.log | sed 's|^/src/||' | sort -u
if [ "$build" -ne 0 ]; then
    echo
    echo "  FAILED: the Linux build does not compile. CI will stop here too, and"
    echo "  skip every test after it."
    exit 1
fi
echo "  OK: builds"

# Warnings fail nothing, in CI or here. They are shown because GCC sees things
# MSVC does not, and this is the only place you will see them before pushing.
warnings=$(grep -c "warning:" build.log || true)
if [ "$warnings" -gt 0 ]; then
    echo
    echo "  $warnings GCC warning(s) - not failures, but MSVC never showed you these:"
    grep -E "warning:" build.log | sed 's|^/src/||' | sort -u | sed 's/^/    /'
fi

# Once rather than CI's twenty. Repetition hunts flakes, and flakes are not
# usually a property of the compiler; `verify.bat flake` does the twenty on
# Windows. What differs on Linux is what the code DOES, and one run shows that.
step "Test"
if ! ctest --test-dir build --output-on-failure -LE smoke; then status=1; fi

step "The games start"
if ! ctest --test-dir build --output-on-failure -L smoke; then status=1; fi

step "The launcher script"
chmod +x run.sh
if ! ./run.sh tests > run.log 2>&1; then
    tail -20 run.log
    echo "  FAILED: run.sh tests"
    status=1
else
    echo "  OK: run.sh tests"
fi

exit $status
