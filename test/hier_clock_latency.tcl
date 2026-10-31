source test/lib/check.tcl
set cons {
	create_clock -name c -period 10 [get_pins {u/clk[1]}]
	set_clock_latency 0.7 [get_pins {u/clk[1]}]
	set_clock_latency -source 2 [get_pins {u/clk[1]}]
	set_output_delay 1 -clock c [get_ports out]
}
set report {report_checks -to [get_ports out] -format full_clock -digits 3}
set native [reference "
	read_liberty test/data/simple.lib
	read_verilog test/data/hier_bus_clk.v
	link_design top
	$cons
	$report"]
foreach pass {stat {splitnets -ports}} {
	yosys design -reset
	link_top test/data/hier_bus_clk.v
	eval $cons
	yosys {*}$pass
	check_eq [sync_losses [log_of opensta_net -sync]] 0
	check_eq [sta_out $report] $native
}
