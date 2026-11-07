#!/bin/sh
# Tests the libraries of the SDK (code/plugin_api/sdk/odf_json.c, odf_http.c) natively with the system compiler. The same files are
# built as QVM code by odf-build whenever a plugin includes their headers (check.sh builds an example that does).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT"
API=../../code/plugin_api
SDK=$API/sdk
FLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -I$SDK -I$API/include"

gcc $FLAGS -o "$OUT/test_json" test_json.c $SDK/odf_json.c
"$OUT/test_json"
gcc $FLAGS -DODF_MODULE_SERVER -o "$OUT/test_http" test_http.c $SDK/odf_http.c
"$OUT/test_http"

gcc $FLAGS -DODF_MODULE_CLIENT -o "$OUT/test_draw" test_draw.c $SDK/odf_draw.c
"$OUT/test_draw"
