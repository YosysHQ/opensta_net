# bus pins of descending and ascending ranges connect as OpenSTA's reader does
source test/lib/check.tcl
set body {
	foreach b {0 1 2 3} {
		puts "u1/A\[$b\] net: [get_full_name [get_nets -of_objects [get_pins u1/A\[$b\]]]]"
	}
	create_clock -period 10 -name clk [get_ports clk]
	set_input_delay 1 -clock clk [get_ports {in[*]}]
	set_output_delay 1 -clock clk [get_ports {out[*]}]
	puts "slack: [format %.2f [worst_slack -max]]"
}
foreach design {bus bus_asc} {
	yosys design -reset
	link_top test/data/$design.v
	check_eq [sta_out $body] [reference "
		read_liberty test/data/simple.lib
		read_verilog test/data/$design.v
		link_design top
		$body"]
}
