#!/bin/sh
# Builds and runs the server login/plugin test with the system gcc.
# The mock plugins are WebAssembly: they need clang with a wasm32 target, for example wasi-sdk (set WASI_SDK to its folder).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
PLUGINS="$HOME_DIR/plugins"
mkdir -p "$PLUGINS"
rm -rf "$PLUGINS"/*.wasm "$HOME_DIR/plugin-data"

if [ -z "$WASI_SDK" ] || [ ! -x "$WASI_SDK/bin/clang" ] && [ ! -x "$WASI_SDK/bin/clang.exe" ]; then
	echo "set WASI_SDK to the folder of wasi-sdk (clang for wasm32)" >&2
	exit 2
fi
S=../../code/server
Q=../../code/qcommon
API=../../code/plugin_api/include

# ---- the mock plugins: one source, built with different names and options ----
PL="$WASI_SDK/bin/clang --target=wasm32-wasip1 --sysroot=$WASI_SDK/share/wasi-sysroot -O2 -mcpu=mvp -mexec-model=reactor -Wl,--no-entry -Wall -I$API -I."
$PL -DPFX=\"mock\" -o "$PLUGINS/mock.wasm" mock_plugin.c
$PL -DPFX=\"trusted\" -o "$PLUGINS/trusted.wasm" mock_plugin.c
$PL -DLITE -DPFX=\"lite\" -o "$PLUGINS/lite.wasm" mock_plugin.c
$PL -DLITE -DPFX=\"lite2\" -o "$PLUGINS/lite2.wasm" mock_plugin.c
$PL -DPFX=\"nogrant\" -o "$PLUGINS/nogrant.wasm" mock_plugin.c
$PL -DLITE -DWANTS -DPFX=\"wants\" -o "$PLUGINS/wants.wasm" mock_plugin.c

# ---- the host, with the real runtime ----
make -s -f ../wasm_lib.mk OUT="$OUT" CC=gcc
INC="-I$S -I$Q -I$API -I../../code/wasm/wamr/core/iwasm/include -I../../code/wasm/wamr/core/shared/platform/odf -I../../code/wasm/wamr/core/shared/platform/include -I../../code/wasm/wamr/core/shared/utils"
gcc -O2 -Wall -Wno-unused-function -DDEDICATED -DODF_PLUGIN_TESTS -DWASM_RUNTIME_API_EXTERN= $INC -I. -o "$OUT/server_auth_test" \
	server_auth_test.c stubs.c \
	$S/sv_auth.c $S/sv_plugin.c $Q/odf_wasm.c \
	$Q/auth_proto.c $Q/monocypher.c $Q/monocypher-ed25519.c $Q/q_shared.c $Q/q_math.c "$OUT/libwamr.a"
"$OUT/server_auth_test" "$HOME_DIR"
