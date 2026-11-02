#!/bin/sh
# Builds a QVM program with q3lcc/q3asm and runs the plugin VM tests (engine VM code: vm.c, vm_x86.c, vm_interpreted.c).
# Q3TOOLS is the folder with q3lcc, q3cpp, q3rcc and q3asm.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
Q3TOOLS=${Q3TOOLS:-/d/Other/Programming/C/oDFs/bin}
mkdir -p "$OUT"
Q=../../code/qcommon

"$Q3TOOLS/q3lcc" -S -DQ3_VM -o "$OUT/qvm_plugin.asm" qvm_plugin.c
"$Q3TOOLS/q3asm" -o "$OUT/plugin.qvm" "$OUT/qvm_plugin.asm" hostile.asm syscalls.asm

case "$(uname -s)" in
MINGW*|MSYS*) ARCH_FLAGS="" ;;
*) ARCH_FLAGS="" ;;
esac

gcc -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-format-truncation $ARCH_FLAGS -I$Q -I. -o "$OUT/plugin_vm_test" \
	harness.c stubs.c \
	$Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c $Q/q_shared.c $Q/q_math.c
"$OUT/plugin_vm_test" "$OUT/plugin.qvm"
