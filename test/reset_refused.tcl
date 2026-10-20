# commands that would clear the constraints or free the sta are refused
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
foreach cmd {{read_verilog test/simple.v} sta::clear_network sta::network_changed sta::delete_all_memory} {
	if {[catch $cmd msg]} { puts "CAUGHT $msg" }
	puts "CLOCKS [llength [all_clocks]]"
}
yosys opt
puts "SLACK [format %.2f [worst_slack -max]]"
