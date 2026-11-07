#!/bin/sh
# Smoke test of a built odf-build:  sh check.sh path/to/odf-build [output folder]
# Builds the examples (and the plugin of tests/client_plugin) and writes the SHA-256 of every plugin to sha256.txt in the
# output folder. The plugins have to be the same file on every system: the CI compares the checksums of all systems.
set -e
here=$(cd "$(dirname "$0")" && pwd)
exe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mkdir -p "${2:-check}"
out=$(cd "${2:-check}" && pwd)
rm -f "$out"/*.qvm "$out"/sha256.txt

cd "$here/examples"
"$exe" --client -o "$out/hello.qvm" hello.c
"$exe" --server -o "$out/hello_server.qvm" hello_server.c
"$exe" --server -o "$out/hello_http.qvm" hello_http.c         # odf-build adds odf_http.c and odf_json.c by itself
"$exe" --client -o "$out/cmd_plugin.qvm" "$here/../../../tests/client_plugin/cmd_plugin.c"

# a program with a mistake has to be refused
printf '#include "odfc_entry.h"\nint OnInit( void ) { return nothing; }\n' > "$out/bad.c"
if "$exe" --client -o "$out/bad.qvm" "$out/bad.c" > "$out/bad.log" 2>&1; then
	echo "odf-build accepted a program that is wrong"
	exit 1
fi
if [ -e "$out/bad.qvm" ]; then
	echo "odf-build wrote a plugin for a program that is wrong"
	exit 1
fi

cd "$out"
for f in hello.qvm hello_server.qvm hello_http.qvm cmd_plugin.qvm; do
	if command -v sha256sum > /dev/null 2>&1; then sum=$(sha256sum "$f"); else sum=$(shasum -a 256 "$f"); fi
	echo "${sum%% *}  $f"
done | tee sha256.txt
echo "odf-build: ok"
