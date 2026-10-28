#!/bin/sh
# Builds and runs the plugin crash guard test with the system gcc (MSYS2 MinGW64 on Windows, any gcc on Linux).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT"
INC="-I../../code/server"

case "$(uname -s)" in
MINGW*|MSYS*)
	PLUGIN="$OUT/crash_plugin.dll"
	EXTRA=""
	;;
*)
	PLUGIN="$OUT/crash_plugin.so"
	EXTRA="-ldl"
	;;
esac

gcc -O2 -Wall -Wextra -shared -fPIC $INC -o "$PLUGIN" crash_plugin.c
gcc -O2 -Wall -Wextra $INC -o "$OUT/guard_test" guard_test.c ../../code/server/sv_plugin_guard.c $EXTRA
"$OUT/guard_test" "$PLUGIN"
