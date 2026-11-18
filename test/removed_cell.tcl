# set_disable_timing survives removed and split cells
source test/lib/check.tcl
yosys read_verilog test/data/removed_cell.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
# a removed disabled cell keeps its paths cut and its neighbours
set_disable_timing [get_cells -of_objects [get_pins -of_objects [get_nets n] -filter "direction==output"]]
# a disabled arc of a split gate is the wire edge into the pin that reads the -from signal
set_disable_timing -from A -to Y [get_cells -of_objects [get_pins -of_objects [get_nets y] -filter "direction==output"]]
# opt_expr -mux_undef makes the mux a wire
# abc -g cmos2 maps the and to two cells
yosys opt_expr -mux_undef
yosys abc -g cmos2
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [llength [find_timing_paths -from [get_ports b] -to [get_ports y] -unconstrained]] 1
check_eq [llength [find_timing_paths -from [get_ports a] -to [get_ports y] -unconstrained]] 0
set sdc [written_sdc]
check_eq [count_lines $sdc {set_disable_timing [get_timing_edges -from [get_pins {*/Y}] -to [get_pins {*/D}]]}] 1
check_eq [count_lines $sdc {set_disable_timing [get_timing_edges -from [get_ports {a}] -to [get_pins {*/A}]]}] 1
