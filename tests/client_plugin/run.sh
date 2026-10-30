#!/bin/sh
# Builds and runs the client plugin host test with the system gcc.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
PLUGINS="$HOME_DIR/plugins"
mkdir -p "$PLUGINS"
C=../../code/client
Q=../../code/qcommon
S=../../code/server
INC="-I$C -I$Q -I$S"

case "$(uname -s)" in
MINGW*|MSYS*)
	EXT=dll
	EXTRA=""
	;;
*)
	EXT=so
	EXTRA="-ldl"
	;;
esac

PL="gcc -O2 -Wall -shared -fPIC -I$C -I. -include stdlib.h"
$PL -o "$PLUGINS/mock.$EXT" mock_plugin.c
$PL -DBAD_VERSION -o "$PLUGINS/badver.$EXT" mock_plugin.c
$PL -DBAD_SIZE -o "$PLUGINS/badsize.$EXT" mock_plugin.c
$PL -DREFUSE -o "$PLUGINS/refuse.$EXT" mock_plugin.c
$PL -DOLD_EXPORTS -o "$PLUGINS/oldexp.$EXT" mock_plugin.c
$PL -o "$PLUGINS/noentry.$EXT" noentry.c

gcc -O2 -Wall -Wno-unused-function $INC -I. -o "$OUT/client_plugin_test" \
	client_plugin_test.c stubs.c \
	$C/cl_plugin.c $S/sv_plugin_guard.c $Q/q_shared.c $Q/q_math.c $EXTRA
"$OUT/client_plugin_test" "$HOME_DIR"
