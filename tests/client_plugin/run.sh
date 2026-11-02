#!/bin/sh
# Builds and runs the client plugin host test with the system gcc.
# The mock plugins are WebAssembly: they need clang with a wasm32 target, for example wasi-sdk (set WASI_SDK to its folder).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
PLUGINS="$HOME_DIR/plugins"
mkdir -p "$PLUGINS"
rm -f "$PLUGINS"/*.wasm "$PLUGINS/disabled"

if [ -z "$WASI_SDK" ] || [ ! -x "$WASI_SDK/bin/clang" ] && [ ! -x "$WASI_SDK/bin/clang.exe" ]; then
	echo "set WASI_SDK to the folder of wasi-sdk (clang for wasm32)" >&2
	exit 2
fi
C=../../code/client
Q=../../code/qcommon
S=../../code/server
API=../../code/plugin_api/include

# ---- the mock plugins ----
PL="$WASI_SDK/bin/clang --target=wasm32-wasip1 --sysroot=$WASI_SDK/share/wasi-sysroot -O2 -mcpu=mvp -mexec-model=reactor -Wl,--no-entry -Wall -I$API -I."
$PL -o "$PLUGINS/mock.wasm" mock_plugin.c
$PL -DBAD_VERSION -DPFX=\"badver\" -o "$PLUGINS/badver.wasm" mock_plugin.c
$PL -DREFUSE -DPFX=\"refuse\" -o "$PLUGINS/refuse.wasm" mock_plugin.c
$PL -DSECOND -DPFX=\"second\" -o "$PLUGINS/second.wasm" mock_plugin.c
$PL -o "$PLUGINS/badimport.wasm" badimport.c
$PL -o "$PLUGINS/noentry.wasm" noentry.c
$PL -o "$PLUGINS/noio.wasm" noio.c
# a name that cannot be used (a space)
cp "$PLUGINS/mock.wasm" "$PLUGINS/bad name.wasm"

# ---- the host, with the real runtime ----
make -s -f ../wasm_lib.mk OUT="$OUT" CC=gcc
INC="-I$C -I$Q -I$S -I$API -I../../code/wasm/wamr/core/iwasm/include -I../../code/wasm/wamr/core/shared/platform/odf -I../../code/wasm/wamr/core/shared/platform/include -I../../code/wasm/wamr/core/shared/utils"
gcc -O2 -Wall -Wno-unused-function -DODF_PLUGIN_TESTS -DWASM_RUNTIME_API_EXTERN= $INC -I. -o "$OUT/client_plugin_test" \
	client_plugin_test.c stubs.c \
	$C/cl_plugin.c $Q/odf_wasm.c $Q/q_shared.c $Q/q_math.c "$OUT/libwamr.a"
"$OUT/client_plugin_test" "$HOME_DIR"
