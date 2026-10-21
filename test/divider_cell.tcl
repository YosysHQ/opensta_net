# a cell named u/x at the top and the cell x in u: write_sdc writes both as
# u/x. a constraint on the top cell stays on it or is reported, never moves
# to the other cell silently
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/divider_cell.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -name clk -period 10
set_input_delay 1 -clock clk [get_ports a]
set_output_delay 1 -clock clk [get_ports {y z}]
set cell [lindex [get_cells -of_objects [get_nets n]] 0]
if {[get_full_name $cell] eq "i2"} { set cell [lindex [get_cells -of_objects [get_nets n]] 1] }
set_false_path -through $cell
puts "NAME [get_full_name $cell] FINDS [get_full_name [get_cells [get_full_name $cell]]]"
proc ends {port} { return [llength [find_timing_paths -to [get_ports $port]]] }
puts "BEFORE y [ends y] z [ends z]"
yosys add -wire dummy top
yosys opensta_net -sync
puts "AFTER y [ends y] z [ends z]"
