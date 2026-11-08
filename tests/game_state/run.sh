#!/bin/sh
# Tests the pure part of the game snapshots (code/server/sv_diff.h) natively with the system compiler.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT"
gcc -O2 -Wall -Wextra -o "$OUT/diff_test" diff_test.c
"$OUT/diff_test"
