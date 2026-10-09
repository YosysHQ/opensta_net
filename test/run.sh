#!/usr/bin/env bash
# Local checks. Each one fails on a regression, nothing relies on eyeballing
set -u

SPIKE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
YOSYS="${YOSYS:-$SPIKE_DIR/deps/yosys/build/yosys}"
STA="${STA:-$SPIKE_DIR/deps/OpenSTA/build/sta}"
PLUGIN="${PLUGIN:-$SPIKE_DIR/opensta_net.so}"
cd "$SPIKE_DIR"

pass=0
fail=0
ok() { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1${2:+ ($2)}"; fail=$((fail + 1)); }
check() { if "${@:2}"; then ok "$1"; else bad "$1"; fi; }

# Output of a .ys or .tcl script, then its exit code
run() {
	local mode=-c
	[[ $1 == *.ys ]] && mode=-s
	"$YOSYS" -m "$PLUGIN" $mode "$1" 2>&1
	echo "EXIT $?"
}

# Output of the reference OpenSTA binary
sta() { "$STA" -no_init -no_splash -exit "$1" 2>&1; }

# Lines between <tag>_BEGIN and <tag>_END, without the plugin's own log lines
between() { awk -v b="$1_BEGIN" -v e="$1_END" '$0 == b {f=1; next} $0 == e {f=0} f' | grep -v '^opensta_net:'; }

# Equal and not empty
same() { check "$1" [ -n "$2" -a "$2" = "$3" ]; }

# Every regex matches the script output. !re must not match, @file=re must match
# the file (removed before the run). EXIT 0 is required unless a check names EXIT
expect() {
	local name=$1 script=$2 re file log
	shift 2
	for re in "$@"; do
		[[ $re == @* ]] && file=${re#@} && rm -f "${file%%=*}"
	done
	log=$(run "$script")
	[[ "$*" == *EXIT* ]] || set -- "$@" '^EXIT 0$'
	for re in "$@"; do
		case $re in
		'!'*) ! grep -qE -- "${re#!}" <<< "$log" ;;
		@*) file=${re#@} && grep -qE -- "${re#*=}" "${file%%=*}" 2> /dev/null ;;
		*) grep -qE -- "$re" <<< "$log" ;;
		esac || { bad "$name" "$re"; return; }
	done
	ok "$name"
}

# The parts of the script output that match re, joined by |, are exactly want
lines() {
	local log got
	log=$(run "$2")
	got=$(grep -oE -- "$3" <<< "$log" | paste -sd '|')
	check "$1" [ "$got" = "$4" -a "$(tail -n 1 <<< "$log")" = "EXIT 0" ]
}

# The <tag> section of the script output equals the reference binary's, which
# reads what the script wrote and must match re
ref() {
	local got want
	got=$(run "$3" | between "$2")
	want=$(sta "$4" | between "$2")
	grep -qE -- "${5:-.}" <<< "$want" || { bad "$1" "reference lacks $5"; return; }
	same "$1" "$want" "$got"
}

rm -f test/*_out.sdc test/*_out.v test/flat_[12].sdc test/uniq_same.v test/uniq_diff.v

# Robustness: each check failed before its fix
expect liberty_missing test/liberty_missing.tcl '^CAUGHT$' '^ALIVE$'

echo "---"
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
