#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# mutate.sh — break the code on purpose and check a test notices.
#
# This is the single most useful technique in this project, and the one that
# keeps finding tests which pass for the wrong reason:
#
#   * a range test that stood a unit at 1600 and aimed at where it STOOD. The
#     unit walked eighty pixels during the shell's flight, so it left the blast
#     whether or not the cannon could reach. Setting the range ten times too
#     far changed nothing it looked at.
#   * a font test that probed the first lit column of a glyph — column zero,
#     where `col * scale` and `col` are both nought. A renderer that squashed
#     every string to a third of its width passed cleanly.
#   * a per-field fallback checked only for `range`, so breaking the fallback
#     on `cost` left the whole suite green.
#   * a determinism test that ran the same case six times and checked the
#     answers agreed. They agreed with or without the rule it was testing.
#
# Every one of those was written in good faith, ran the code it meant to run,
# and would not have noticed the code being wrong. There is no way to tell the
# difference by reading. You have to break it and look.
#
# USAGE
#     ./mutate.sh <file> <find> <replace> [ctest-name]
#
#     ./mutate.sh ../include/engine/View.h \
#         "worldX - camera.x" "worldX + camera.x" render_tests
#
# `find` is matched LITERALLY, not as a regular expression, so you can paste a
# line of C++ in without escaping anything. That matters: half the time this
# was done by hand with sed, the substitution silently failed to apply because
# the pattern contained a `|` or a `/` or a `&`, and a mutation that never
# applied looks exactly like a mutation nothing caught.
#
# WHAT THE RESULT MEANS
#     tests FAILED  — good. The tests would notice this bug.
#     tests PASSED  — bad. Either the mutation is harmless, or you have found
#                     a test that does not test what it claims to.
#
# The file is always restored, including if the build fails or you interrupt it.
# ----------------------------------------------------------------------------

set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

if [ "$#" -lt 3 ]; then
    sed -n '4,45p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

FILE="$1"
FIND="$2"
REPLACE="$3"
TARGET="${4:-}"

if [ ! -f "$FILE" ]; then
    echo "No such file: $FILE" >&2
    exit 1
fi

BACKUP="$(mktemp)"
cp "$FILE" "$BACKUP"

# Restored no matter how this exits — a mutation left in the working tree is
# far worse than one that never ran.
restore() {
    cp "$BACKUP" "$FILE"

    # The timestamp is bumped deliberately, and this is not fussiness. A
    # restored file can end up OLDER than the object files built from the
    # mutated source, at which point the build system decides there is nothing
    # to do and leaves you with correct source and mutated binaries — a state
    # that looks fine, tests broken, and takes a long time to understand. The
    # Windows half of this script did exactly that once.
    touch "$FILE"
    rm -f "$BACKUP"
}
trap restore EXIT INT TERM

echo "Mutating $FILE"
echo "   from: $FIND"
echo "     to: $REPLACE"

# \Q...\E makes the pattern literal, so C++ punctuation needs no escaping.
perl -0pi -e 'BEGIN { $find = shift @ARGV; $repl = shift @ARGV }
              s/\Q$find\E/$repl/g' "$FIND" "$REPLACE" "$FILE"

# Verified rather than assumed. A mutation that did not apply produces a green
# run that looks like a surviving mutant, which is the most misleading result
# this script could give.
if ! grep -qF -- "$REPLACE" "$FILE"; then
    echo
    echo "  THE MUTATION DID NOT APPLY."
    echo "  The text was not found. Copy the line exactly as it appears in the"
    echo "  file, including indentation inside the quotes if you need it."
    exit 2
fi

echo "Building..."
if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null || exit 1
fi

if ! cmake --build build --config Release >/dev/null 2>&1; then
    echo
    echo "  The mutated code does not compile."
    echo "  That is not a result: pick a change that builds, so the tests get"
    echo "  a chance to have an opinion."
    exit 2
fi

echo "Testing..."
echo
if [ -n "$TARGET" ]; then
    ctest --test-dir build -C Release --output-on-failure -R "^${TARGET}$"
else
    ctest --test-dir build -C Release --output-on-failure
fi
RESULT=$?

echo
echo "=============================================================="
if [ "$RESULT" -ne 0 ]; then
    echo "  CAUGHT — the tests noticed. This bug could not ship silently."
else
    echo "  SURVIVED — every test passed with the code broken."
    echo
    echo "  Either this change genuinely does not matter, or a test that looks"
    echo "  like it covers this does not. Both are worth knowing; the second is"
    echo "  worth fixing before you trust that test again."
fi
echo "=============================================================="

# The trap restores the file as this exits, but the BUILD still holds the
# mutated objects until something rebuilds them. Doing it here, and checking
# the result, is the difference between "restored" and "safe to walk away
# from".
restore
trap - EXIT INT TERM

echo
echo "Rebuilding the unmutated code..."
cmake --build build --config Release >/dev/null 2>&1

echo "Confirming the tree is back to green..."
if [ -n "$TARGET" ]; then
    ctest --test-dir build -C Release --quiet -R "^${TARGET}$" >/dev/null 2>&1
else
    ctest --test-dir build -C Release --quiet >/dev/null 2>&1
fi

if [ $? -ne 0 ]; then
    echo
    echo "  WARNING: tests are still failing after the restore."
    echo "  The source is back, so this is a stale build. Run:"
    echo "      cmake --build build --config Release --clean-first"
    exit 1
fi
echo "  Clean."
exit 0
