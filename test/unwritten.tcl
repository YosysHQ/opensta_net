# the sync applies again what write_sdc skips or writes raw, the commands after the epoch end and user properties
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk -network_latency_included [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
set_clock_gating_check -setup 0.5 [get_clocks clk]
set_voltage -object_list [get_nets n1] 1.1
define_property -object_type instance -type string tag
define_property -object_type pin -type string tag
define_property -object_type liberty_cell -type string tag
set_property [get_cells u_inv1] tag CELL
set_property [get_pins u_dff0/Q] tag PIN
set_property [get_lib_cells simple/INV] tag LIB
yosys add -wire dummy top
set_clock_uncertainty 0.7 [get_clocks clk]
yosys opensta_net -sync
foreach kept $::ysta::kept {
	puts "KEPT [::ysta::sdc_text [lindex $kept 1]]"
}
puts "AFTER [get_property [get_cells u_inv1] tag] [get_property [get_pins u_dff0/Q] tag]"
puts "LIB [get_property [get_lib_cells simple/INV] tag]"
write_sdc -no_timestamp test/unwritten_out.sdc
