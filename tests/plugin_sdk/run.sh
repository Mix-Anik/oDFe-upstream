#!/bin/sh
# Builds the library of the SDK (code/plugin_api/sdk) with odf-build and runs a QVM program that tests it, comparing with the C
# library of the host where there is one. Runs in both modes of the VM.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
PYTHON=${PYTHON:-python}
mkdir -p "$OUT"
Q=../../code/qcommon
API=../../code/plugin_api
SDK=$API/sdk
$PYTHON $API/gen_api.py --api sdk.api "$OUT" > /dev/null

${MAKE:-make} -s -C $SDK CC=gcc OUT="$(pwd)/$OUT" > /dev/null
"$OUT/odf-build" --test -I"$OUT/include" --syscalls "$OUT/include/odft_syscalls.asm" -I. -o "$OUT/plugin.qvm" sdk_test.c

gcc -O2 -g -Wall -Wno-unused-function -Wno-format-truncation -I$Q -I$API -I"$OUT/generated" -I. -I../plugin_vm -o "$OUT/sdk_test" \
	harness.c ../plugin_vm/stubs.c \
	$Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c $Q/q_shared.c $Q/q_math.c
"$OUT/sdk_test" "$OUT/plugin.qvm"
