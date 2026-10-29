#!/usr/bin/env bash
# run each test/<name>.tcl in its own yosys: bash test/run.sh [name...]
# the log of each test goes to test/out/<name>.log
set -u
cd "$(dirname "$0")/.."
export YOSYS="${YOSYS:-$PWD/deps/yosys/build/yosys}"
export STA="${STA:-$PWD/deps/OpenSTA/build/sta}"
export PLUGIN="${PLUGIN:-$PWD/opensta_net.so}"
mkdir -p test/out

names=("$@")
[ $# -gt 0 ] || names=($(basename -s .tcl test/*.tcl))
fail=0
for name in "${names[@]}"; do
	log="test/out/$name.log"
	"$YOSYS" -m "$PLUGIN" -c "test/$name.tcl" > "$log" 2>&1
	code=$?
	if [ $code -eq 0 ]; then
		echo "PASS $name"
		continue
	fi
	echo "FAIL $name (exit $code, $log)"
	grep '^ERROR' "$log" | tail -n 1
	fail=$((fail + 1))
done
echo "$((${#names[@]} - fail)) passed, $fail failed"
[ $fail -eq 0 ]
