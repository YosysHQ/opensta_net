# the sync applies again what write_sdc skips
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk -network_latency_included [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
set_clock_gating_check -setup 0.5 [get_clocks clk]
yosys add -wire dummy top
yosys opensta_net -sync
foreach kept $::ysta::kept {
	puts "KEPT [::ysta::sdc_text [lindex $kept 1]]"
}
