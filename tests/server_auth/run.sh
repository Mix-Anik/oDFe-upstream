#!/bin/sh
# Builds and runs the server login/plugin test with the system gcc.
# The mock plugins are QVM programs: they need q3lcc and q3asm (Q3TOOLS is the folder they are in).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
PLUGINS="$HOME_DIR/plugins"
Q3TOOLS=${Q3TOOLS:-/d/Other/Programming/C/oDFs/bin}
mkdir -p "$PLUGINS"
rm -rf "$PLUGINS"/*.qvm "$HOME_DIR/plugin-data"

S=../../code/server
Q=../../code/qcommon
API=../../code/plugin_api/include

# the two calls only the test build of the engine has come after the calls of the API
N=$(grep -c '^equ' "$API/odfs_syscalls.asm")
{ echo "code"; echo "equ TestPull -$((N + 1))"; echo "equ TestPush -$((N + 2))"; } > "$OUT/odfs_test.asm"

# ---- the mock plugins: one source, built with different names and options ----
build() {
	name=$1; shift
	"$Q3TOOLS/q3lcc" -S -DQ3_VM "$@" -I$API -I. -o "$OUT/$name.asm" mock_plugin.c
	"$Q3TOOLS/q3asm" -o "$PLUGINS/$name.qvm" "$OUT/$name.asm" "$API/odfs_syscalls.asm" "$OUT/odfs_test.asm"
}
build mock '-DPFX="mock"'
build trusted '-DPFX="trusted"'
build lite -DLITE '-DPFX="lite"'
build lite2 -DLITE '-DPFX="lite2"'
build nogrant '-DPFX="nogrant"'
build wants -DLITE -DWANTS '-DPFX="wants"'
build big -DSTOREONLY '-DPFX="big"'
build tiny -DSTOREONLY '-DPFX="tiny"'

# ---- the host, with the real VM code of the engine ----
INC="-I$S -I$Q -I$API"
gcc -O2 -Wall -Wno-unused-function -DDEDICATED -DODF_PLUGIN_TESTS $INC -I. -o "$OUT/server_auth_test" \
	server_auth_test.c stubs.c \
	$S/sv_auth.c $S/sv_plugin.c $Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c \
	$Q/auth_proto.c $Q/monocypher.c $Q/monocypher-ed25519.c $Q/q_shared.c $Q/q_math.c -lm
"$OUT/server_auth_test" "$HOME_DIR"
