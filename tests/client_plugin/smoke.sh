#!/bin/sh
# Runs a real client plugin (a .qvm) in the host of the engine against the fake engine, compiled and interpreted, and times it.
#   sh smoke.sh path/to/plugin.qvm [frames] [cvar=value ...]
set -e
cd "$(dirname "$0")"
QVM=$1
shift
OUT=${OUT:-build}
HOME_DIR="$OUT/smokehome"
PLUGINS="$HOME_DIR/plugins"
NAME=$(basename "$QVM" .qvm)
mkdir -p "$PLUGINS"
rm -f "$PLUGINS"/*.qvm "$PLUGINS/disabled"
cp "$QVM" "$PLUGINS/$NAME.qvm"

C=../../code/client
Q=../../code/qcommon
S=../../code/server
API=../../code/plugin_api/include
INC="-I$C -I$Q -I$S -I$API"
gcc -std=gnu11 -O2 -Wall -Wno-unused-function -Wno-array-bounds $INC -I. -o "$OUT/plugin_smoke" \
	plugin_smoke.c stubs.c \
	$C/cl_plugin.c $Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c $Q/q_shared.c $Q/q_math.c -lm 2>&1 | grep -v "^ \|^In \|note:\|inlined\|vm_optimize\|MOP_UNDEF" || true
# a mode at a time: the fake engine keeps the cvars a plugin created, so a second start in the same process is not the first
"$OUT/plugin_smoke" "$HOME_DIR" "$NAME" jit "$@"
"$OUT/plugin_smoke" "$HOME_DIR" "$NAME" interp "$@"
