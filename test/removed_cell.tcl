# a disabled cell that a pass removes keeps its paths cut and its neighbours, a
# disabled arc of a gate that abc maps to two cells stays on the -from cell
yosys read_verilog test/removed_cell.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/moved_pins.lib -link
set_disable_timing [get_cells -of_objects [get_pins -of_objects [get_nets n] -filter "direction==output"]]
set_disable_timing -from A -to Y [get_cells -of_objects [get_pins -of_objects [get_nets y] -filter "direction==output"]]
yosys opt_expr -mux_undef
yosys abc -g cmos2
yosys opensta_net -sync
puts "PATHS [llength [find_timing_paths -from [get_ports b] -to [get_ports y] -unconstrained]]"
write_sdc -no_timestamp test/removed_cell_out.sdc
