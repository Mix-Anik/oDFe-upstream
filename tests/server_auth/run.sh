#!/bin/sh
# Builds and runs the server login/plugin test with the system gcc.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
mkdir -p "$HOME_DIR/plugins"
S=../../code/server
Q=../../code/qcommon
INC="-I$S -I$Q"

case "$(uname -s)" in
MINGW*|MSYS*)
	PLUGIN="$HOME_DIR/plugins/mock.dll"
	EXTRA=""
	;;
*)
	PLUGIN="$HOME_DIR/plugins/mock.so"
	EXTRA="-ldl"
	;;
esac

gcc -O2 -Wall -shared -fPIC $INC -o "$PLUGIN" mock_plugin.c -include stdlib.h
gcc -O2 -Wall -Wno-unused-function -DDEDICATED $INC -I. -o "$OUT/server_auth_test" \
	server_auth_test.c stubs.c \
	$S/sv_auth.c $S/sv_plugin.c $S/sv_plugin_guard.c \
	$Q/auth_proto.c $Q/monocypher.c $Q/monocypher-ed25519.c $Q/q_shared.c $Q/q_math.c $EXTRA
"$OUT/server_auth_test" "$HOME_DIR"
