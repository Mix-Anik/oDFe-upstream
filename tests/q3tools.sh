# Sourced by the run.sh of the tests that build QVM programs, from the folder of the test: finds the folder with q3lcc and q3asm.
# Q3TOOLS names it; without it the bin folder of an oDFs checkout next to this repository is used.
if [ -z "$Q3TOOLS" ]; then
	Q3TOOLS=$(cd ../../../oDFs/bin 2>/dev/null && pwd) || Q3TOOLS=
fi
if [ ! -f "$Q3TOOLS/q3lcc" ] && [ ! -f "$Q3TOOLS/q3lcc.exe" ]; then
	echo "q3lcc and q3asm not found: set Q3TOOLS to the folder they are in (the bin folder of the oDFs repository)" >&2
	exit 1
fi
