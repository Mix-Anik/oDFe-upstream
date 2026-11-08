#!/bin/sh
# Runs the real records plugin (records.qvm of the df-records-plugin project) in the real server plugin code of the engine, with
# a made-up engine and API (tests/server_auth/stubs.c). RECORDS_QVM is the plugin (default: next to this checkout).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
PLUGINS="$HOME_DIR/plugins"
RECORDS_QVM=${RECORDS_QVM:-../../../df-records-plugin/build/records/records.qvm}
mkdir -p "$PLUGINS"
rm -rf "$PLUGINS"/*.qvm "$HOME_DIR/plugin-data"
cp "$RECORDS_QVM" "$PLUGINS/records.qvm"

S=../../code/server
Q=../../code/qcommon
API=../../code/plugin_api/include
A=../server_auth

gcc -O2 -Wall -Wno-unused-function -DDEDICATED -I$S -I$Q -I$API -I$A -o "$OUT/records_e2e" \
	records_e2e.c $A/stubs.c \
	$S/sv_auth.c $S/sv_plugin.c $Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c \
	$Q/auth_proto.c $Q/monocypher.c $Q/monocypher-ed25519.c $Q/q_shared.c $Q/q_math.c -lm
"$OUT/records_e2e" "$HOME_DIR"
