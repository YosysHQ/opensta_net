# two cells with one hdlname keep their own names, none hides the other
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys setattr -set hdlname {"dup"} c:u_inv1 c:u_inv2
yosys opensta_net -liberty test/simple.lib -link
foreach cell [get_cells *] {
	puts "CELL [get_full_name $cell]"
}
create_clock -name clk -period 10 [get_ports clk]
set_output_delay 1 -clock clk [get_ports out]
report_checks -to [get_ports out] -format end
