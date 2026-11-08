#!/bin/sh
# Tests CommandRegister( name, handler ) (a handler function per console command): builds cmd_plugin.c as a QVM, starts it in the real client host and
# runs the commands it registered, compiled and interpreted.
set -e
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT"
${MAKE:-make} -s -C ../../code/plugin_api/sdk CC=gcc OUT="$(pwd)/$OUT" > /dev/null
"$OUT/odf-build" --client -o "$OUT/cmdtest.qvm" cmd_plugin.c

LOG="$OUT/cmd_test.log"
ODF_SMOKE_COMMANDS=1 sh smoke.sh "$OUT/cmdtest.qvm" 10 > "$LOG"
cat "$LOG"

fail=0
# expect COUNT LINE: the line (the whole of it) has to be in the log COUNT times in each of the two modes
expect() {
	n=$(grep -c -x -F -- "$2" "$LOG" || true)
	if [ "$n" != "$(( $1 * 2 ))" ]; then
		echo "FAIL: '$2' found $n times, wanted $(( $1 * 2 ))"
		fail=1
	fi
}
for t in "add alpha: 1" "add beta: 1" "add alpha again: 0" "add quit: 0" "add no handler: 0" "add Bad Name: 0" "add too long: 0"; do
	expect 1 "  [cmdtest] $t"
done
expect 2 "  command alpha -> [cmdtest] alpha: 3 args, first 'one'"
expect 1 "  command beta -> [cmdtest] beta: run 1"
expect 1 "  command beta -> [cmdtest] beta: run 2"
if [ $fail = 0 ]; then echo "CommandRegister: passed"; else exit 1; fi
