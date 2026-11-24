# design -push leaves no top and no network, design -pop brings both back with the constraints
source test/lib/check.tcl
link_top test/data/sync.v
create_clock -period 10 -name clk [get_ports clk]
set_output_delay 1 -clock clk [get_ports out]
yosys design -push
check_eq [sta::network_is_linked] 0
yosys design -pop
check_eq [llength [all_clocks]] 1
check_eq [count_lines [sta_out {report_checks -format end}] {out (output) * 9.00 * 0.40 * 8.60 (MET)}] 1
