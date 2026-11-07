#!/bin/sh
# Generates the code of test.api, builds a QVM program that calls it and runs the dispatcher tests; also checks that the files in
# code/plugin_api are what their descriptions generate.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
Q3TOOLS=${Q3TOOLS:-/d/Other/Programming/C/oDFs/bin}
PYTHON=${PYTHON:-python}
mkdir -p "$OUT"
Q=../../code/qcommon
API=../../code/plugin_api

$PYTHON $API/gen_api.py --check
# the data the documentation site reads
$PYTHON $API/gen_api.py --json "$OUT/json" > /dev/null
$PYTHON $API/gen_api.py --api test.api "$OUT" > /dev/null

"$Q3TOOLS/q3lcc" -S -DQ3_VM -I$API/include -I"$OUT/include" -I. -o "$OUT/qvm_plugin.asm" qvm_plugin.c
"$Q3TOOLS/q3asm" -o "$OUT/plugin.qvm" "$OUT/qvm_plugin.asm" "$OUT/include/odft_syscalls.asm"

gcc -O2 -g -Wall -Wno-unused-function -Wno-format-truncation -I$Q -I$API -I"$OUT/generated" -I. -I../plugin_vm -o "$OUT/plugin_api_test" \
	harness.c ../plugin_vm/stubs.c \
	$Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c $Q/q_shared.c $Q/q_math.c -lm
"$OUT/plugin_api_test" "$OUT/plugin.qvm"
