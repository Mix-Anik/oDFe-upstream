#!/bin/sh
# Tests the route of a demo (code/client/cl_demoroute.c) natively with the system compiler and the real message code of the engine.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT/demos/temp"
Q=../../code/qcommon
gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I$Q -o "$OUT/route_test" route_test.c ../../code/client/cl_demoroute.c $Q/msg.c $Q/huffman_static.c $Q/q_shared.c $Q/q_math.c
"$OUT/route_test"
