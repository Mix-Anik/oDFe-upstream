#!/bin/sh
# Builds and runs the client plugin host test with the system gcc.
# The mock plugins are QVM programs: they need q3lcc and q3asm (Q3TOOLS is the folder they are in).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
HOME_DIR="$OUT/home"
PLUGINS="$HOME_DIR/plugins"
Q3TOOLS=${Q3TOOLS:-/d/Other/Programming/C/oDFs/bin}
mkdir -p "$PLUGINS"
rm -f "$PLUGINS"/*.qvm "$PLUGINS/disabled"

C=../../code/client
Q=../../code/qcommon
S=../../code/server
API=../../code/plugin_api/include

# the two calls only the test build of the engine has come after the calls of the API
N=$(grep -c '^equ' "$API/odfc_syscalls.asm")
{ echo "code"; echo "equ TestPull -$((N + 1))"; echo "equ TestPush -$((N + 2))"; } > "$OUT/odfc_test.asm"

# build NAME SOURCE SYSCALLS [compiler options]
build() {
	name=$1; src=$2; sys=$3; shift 3
	"$Q3TOOLS/q3lcc" -S -DQ3_VM "$@" -I$API -I. -o "$OUT/$name.asm" "$src"
	"$Q3TOOLS/q3asm" -o "$PLUGINS/$name.qvm" "$OUT/$name.asm" $sys
}

SYS="$API/odfc_syscalls.asm $OUT/odfc_test.asm"
build mock mock_plugin.c "$SYS"
build badver mock_plugin.c "$SYS" -DBAD_VERSION '-DPFX="badver"'
build refuse mock_plugin.c "$SYS" -DREFUSE '-DPFX="refuse"'
build second mock_plugin.c "$SYS" -DSECOND '-DPFX="second"'
build badimport badimport.c "$SYS"
build noentry noentry.c "$API/odfs_syscalls.asm"
build noio noio.c ""
# a name that cannot be used (a space)
cp "$PLUGINS/mock.qvm" "$PLUGINS/bad name.qvm"

# ---- the host, with the real VM code of the engine ----
INC="-I$C -I$Q -I$S -I$API"
gcc -O2 -Wall -Wno-unused-function -DODF_PLUGIN_TESTS $INC -I. -o "$OUT/client_plugin_test" \
	client_plugin_test.c stubs.c \
	$C/cl_plugin.c $Q/plugin_vm.c $Q/vm.c $Q/vm_x86.c $Q/vm_interpreted.c $Q/q_shared.c $Q/q_math.c
"$OUT/client_plugin_test" "$HOME_DIR"
