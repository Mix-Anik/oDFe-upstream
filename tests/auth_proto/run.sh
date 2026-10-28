#!/bin/sh
# Builds and runs the login protocol test with the system gcc.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT"
Q=../../code/qcommon
gcc -O2 -Wall -Wextra -I"$Q" -o "$OUT/auth_proto_test" auth_proto_test.c "$Q/auth_proto.c" "$Q/monocypher.c" "$Q/monocypher-ed25519.c"
"$OUT/auth_proto_test"
