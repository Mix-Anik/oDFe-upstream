#!/bin/sh
# The structs of the API must have the same layout for the engine (gcc) and for a plugin (q3lcc): see gen_layout.py.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
Q3TOOLS=${Q3TOOLS:-/d/Other/Programming/C/oDFs/bin}
PYTHON=${PYTHON:-python}
API=../../code/plugin_api
mkdir -p "$OUT"
$PYTHON gen_layout.py "$OUT/layout.inc" $API/include/odfc_types.h $API/include/odfs_types.h
# q3cpp takes include folders relative to the source, so the source is built where it is
cp layout_check.c "$OUT/layout_check.c"
gcc -fsyntax-only -Wall -I$API/include -I"$OUT" "$OUT/layout_check.c"
cd "$OUT"
"$Q3TOOLS/q3lcc" -S -N -DQ3_VM -I../$API/include -I../$API/sdk/libc -I. -o layout_check.asm layout_check.c
echo "layout: gcc and q3lcc agree with the rules of C"
