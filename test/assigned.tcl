# set_assigned_delay, set_assigned_check and set_assigned_transition
source test/lib/check.tcl
link_top test/data/simple.v
proc edge_delay {from to} {
	get_property [lindex [get_timing_edges -from [get_pins $from] -to [get_pins $to]] 0] delay_max_rise
}
proc assigned {} {
	list [edge_delay u_inv1/A u_inv1/Y] [edge_delay u_dff0/CLK u_dff0/D] [get_property [get_pins u_inv2/A] slew_max_rise]
}
proc rebuild {} {
	yosys add -wire extra[incr ::wires] 1
	check_eq [sync_losses [log_of opensta_net -sync]] 0
}
set_assigned_delay -cell -from [get_pins u_inv1/A] -to [get_pins u_inv1/Y] 0.5
set_assigned_check -setup -from [get_pins u_dff0/CLK] -to [get_pins u_dff0/D] 0.7
set_assigned_transition 0.3 [get_pins u_inv2/A]
check_eq [assigned] {0.500000 0.700000 0.300000}
rebuild
check_eq [assigned] {0.500000 0.700000 0.300000}
sta::remove_delay_slew_annotations
set removed [assigned]
rebuild
check_eq [assigned] $removed
