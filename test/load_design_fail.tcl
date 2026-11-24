# a failed ysta::load_design gives the yosys error and leaves no network of the old design
set ::env(OPENSTA_NET_CONFORMANCE) 1
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -name c -period 10 [get_ports clk]
get_clocks c
lassign [sta {list [catch {::ysta::load_design {} /nonexistent.v top} error] $error [sta::network_is_linked]}] failed error linked
check_eq [list $failed $linked [sta {array size ::ysta::live_handles}]] {1 0 0}
check_eq [string match {ysta::load_design failed, *nonexistent.v*} $error] 1
