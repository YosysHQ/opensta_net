#!/usr/bin/env bash
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

# output of a .ys or .tcl script, then its exit code
run() {
	local mode=-c
	[[ $1 == *.ys ]] && mode=-s
	"$YOSYS" -m "$PLUGIN" $mode "$1" 2>&1
	echo "EXIT $?"
}

# output of the reference OpenSTA binary
sta() { "$STA" -no_init -no_splash -exit "$1" 2>&1; }

# lines between <tag>_BEGIN and <tag>_END
between() { awk -v b="$1_BEGIN" -v e="$1_END" '$0 == b {f=1; next} $0 == e {f=0} f' | grep -v '^opensta_net:'; }

# equal and not empty
same() { check "$1" [ -n "$2" -a "$2" = "$3" ]; }

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

lines() {
	local log got
	log=$(run "$2")
	got=$(grep -oE -- "$3" <<< "$log" | paste -sd '|')
	check "$1" [ "$got" = "$4" -a "$(tail -n 1 <<< "$log")" = "EXIT 0" ]
}

ref() {
	local got want
	got=$(run "$3" | between "$2")
	want=$(sta "$4" | between "$2")
	grep -qE -- "${5:-.}" <<< "$want" || { bad "$1" "reference lacks $5"; return; }
	same "$1" "$want" "$got"
}

# constraints that the sync after the passes reports lost or changed, as
# command and value, their count and the exit code
track() {
	local got
	got=$(PASS="$2" run test/track.tcl | sed -nE -e 's/^(EXIT [0-9]+)$/\1/p' \
		-e 's/.*(synced after design change, [0-9]+).*/\1/p' \
		-e 's/.*sync: `([a-z_]+) .* ([0-9]+\.[0-9]+)` lost or changed.*/\1 \2/p' -e 't' \
		-e 's/.*sync: `([a-z_]+) .*` lost or changed.*/\1/p' | LC_ALL=C sort | paste -sd '|')
	check "$1" [ "$got" = "$3" ]
}

rm -f test/*_out.sdc test/*_out.v test/flat_[12].sdc test/uniq_same.v test/uniq_diff.v

expect m1 test/m1.ys 'insts: 4 pins: 11 nets: 6' 'slack \(MET\)'
lines m4 test/m4.ys 'slack [a-z_ ]*: [0-9.]+' 'slack baseline: 8.60|slack after yosys edit: 8.70'
same optflow "$(run test/optflow.ys | grep -E '^(insts|slack):' | tail -n 2)" "$(run test/optflow_fresh.ys | grep -E '^(insts|slack):')"
expect memo test/memo.ys 'linked before: 1' 'linked after: 1' '^cells: u_dff0 u_inv0 u_inv2 u_x$' 'refused: NO'
lines rtlil_attr test/rtlil_attr.tcl '^(NET|LINKED) .*' 'NET n1|LINKED 1|NET zz|LINKED 1|NET n1'
lines top test/top.ys '^insts: [0-9]+|design changed since link' 'insts: 2|design changed since link|insts: 3'
expect tie test/tie.ys 'tie nets: 1'
expect nets_exact test/nets_exact.tcl '^EQUAL 1$' '^FAST 1$'
ref handoff HANDOFF test/handoff.ys test/handoff_ref.tcl slack
ref barrier BARRIER test/barrier.ys test/barrier_ref.tcl 'slack \(MET\)'
ref order ORDER test/order.ys test/order_ref.tcl
ref const CONST test/const.ys test/const_ref.tcl
same const_handoff "$(sta test/const_ref.tcl | between CONST)" "$(sta test/const_handoff.tcl | between CONST)"
uniq_got=$(run test/uniq.ys | between UNIQ)
uniq_want=$(for v in same diff; do UNIQ_V=test/uniq_$v.v sta test/uniq_ref.tcl | between UNIQ; done)
same uniq "$uniq_want" "$uniq_got"
check uniq_modules [ "$(grep -c '^module' test/uniq_same.v)" = 2 -a "$(grep -c '^module' test/uniq_diff.v)" = 3 ]
same uniq_fold "$(UNIQ_V=test/uniq.v sta test/uniq_ref.tcl | between UNIQ)" "$(run test/uniq_fold.ys | between UNIQ)"
for t in bus bus_asc; do
	same $t "$(sta test/${t}_ref.tcl)" "$(run test/$t.ys | grep -E '^(u1|slack)')"
done
same bus1 "$(sta test/bus1_ref.tcl | grep '^BUS1')" "$(run test/bus1.ys | grep '^BUS1')"
stress_ref=$(sta test/stress_ref.tcl | sed 's/line [0-9]*/line N/')
stress_out=$(run test/stress.ys | sed -n '/^--- ports ---$/,$p' | sed '/^End of script/,$d' | sed 's/line [0-9]*/line N/')
check stress cmp -s <(diff <(echo "$stress_ref") <(echo "$stress_out")) test/stress_waiver

# sync
lines sync_tcl_autosync test/sync_tcl.ys '^(SLACK|CELLS) .*|synced after design change, [0-9]+' \
	'SLACK 8.60|synced after design change, 0|synced after design change, 0|SLACK 8.60|CELLS 6'
expect sync_rawtcl_checkpoint test/sync_rawtcl.ys 'synced after design change, 0 ' '@test/sync_rawtcl_out.sdc=^set_output_delay 2.*\{out3\}'
expect sync_unreported_first test/sync_unreported.ys 'synced after design change, 0 ' '@test/sync_rawtcl_out.sdc=^set_output_delay 2.*\{out3\}'
# hierarchical constraints survive flatten
lines flat_sync test/flat.ys '^(SLACK|CELL|PIN|NET|WILD|QNET) .*|synced after design change, [0-9]+' \
	'SLACK 1.65|CELL u1/r u1/u2/j|PIN u1/r/Q u1/u2/k/Y|NET u1/mid u1/u2/n|WILD u1/u2/j u1/u2/k u1/r/D|QNET u1/y|synced after design change, 0|SLACK 1.65|CELL u1/r u1/u2/j|PIN u1/r/Q u1/u2/k/Y|NET u1/mid u1/u2/n|WILD u1/u2/j u1/u2/k u1/r/D|QNET y'
check flat_sdc cmp -s test/flat_1.sdc test/flat_2.sdc
# constraints on renamed objects stay, by the anchors
track track_autoname autoname 'EXIT 0|synced after design change, 0'
track track_flatten flatten 'EXIT 0|synced after design change, 0'
track track_unprotect 'flatten;opensta_net -unprotect;select -assert-none t:$barrier' \
	'EXIT 0|synced after design change, 0|synced after design change, 0'
expect stale_rawtcl test/stale_rawtcl.ys 'synced after design change, 0 ' '^SLACK 8.85$' '^CELLS 10$'
expect unwritten test/unwritten.tcl 'synced after design change, 0 ' '^KEPT set_input_delay 1 -clock clk -network_latency_included \[get_ports \{in\}\]$' \
	'^KEPT set_clock_gating_check -setup 0.5 \[get_clocks \{clk\}\]$' '^AFTER CELL PIN$' '^LIB LIB$'
expect sync_cmds test/sync_cmds.ys 'synced after design change, 2 constraint' '@test/sync_cmds_out.sdc=^set_max_delay' '@test/sync_cmds_out.sdc=^set_output_delay'
expect autosync_push test/autosync_push.tcl 'PUSHED linked: 0' 'POPPED clocks: 1' '\(MET\)'
expect multimode test/multimode.tcl '^MODE mB$' '^mB clkB$' '^mA clkA$'
expect parasitics_warned test/parasitics.tcl 'drops the loaded parasitics'
expect liberty_missing test/liberty_missing.tcl '^CAUGHT$' '^ALIVE$'
expect boot_fail test/boot_fail.tcl 'ERROR: OpenSTA: cannot wrap find_fanout_insts' '!^BOOTED' '!^ALIVE' '^EXIT 1$'
expect eval_error test/eval_error.tcl '^CAUGHT$' '^ALIVE$'
lines link_fail test/link_fail.tcl '^(SLACK|SYNC|AUTOSYNC) .*' 'SLACK 8.60|SYNC 0 0|SLACK 8.60|AUTOSYNC 0|SLACK 8.60'
expect sdc_write_fail test/sdc_write_fail.tcl '^SYNC 0 1$' '^CLOCKS 1$' '^KEPT 1 1 1$' '!lacks'
expect eval_quotes test/eval_quotes.ys '^42$'
expect fanout_empty test/fanout_empty.tcl '^FANOUT 0 0$' '^FANIN 0 0$'
expect shared_names test/shared_names.ys '1 net\(s\) have the name of another net'
expect shared_thru test/shared_thru.tcl '^AFTER y 0 z 1$' 'synced after design change, 0 '
expect divider_cell test/divider_cell.tcl '^BEFORE y 1 z 0$' '^AFTER y 1 z 0$' 'synced after design change, 0 '
expect dup_hdlname test/dup_hdlname.tcl '^CELL u_inv1$' '^CELL u_inv2$' '^out \(output\) .*\(MET\)$'
expect noop_clean test/noop_clean.ys 'built network' '!synced after design change'
expect sdc_error test/sdc_error.tcl '^CAUGHT$' '^CLOCKS 1$' 'sdc_error.sdc line 3'
expect lost_rename test/lost_rename.tcl 'synced after design change, 0 constraint' '^LOAD w0$'
expect stale_handle test/stale_handle.tcl '^CAUGHT Error: 2931' '^FRESH u_inv2/Y$'
expect id_overflow test/id_overflow.tcl '^REFUSED$' 'more than the 32-bit object ids hold'
expect reset_refused test/reset_refused.tcl '!^CLOCKS 0' 'CAUGHT Error: 2929 delete_all_memory' '^SLACK 8.60$'
for t in attr_refused read_verilog_refused clear_network_refused; do
	expect $t test/$t.tcl '^CAUGHT Error: 2929' '^SLACK 8.60$'
done

echo "---"
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
