# a cell named u/x at the top and the cell x in u: write_sdc writes both as
# u/x. A constraint on the top cell stays on it or is reported, it never
# moves to the other cell silently
source test/lib/check.tcl
link_top test/data/divider_cell.v
create_clock -name clk -period 10
set_input_delay 1 -clock clk [get_ports a]
set_output_delay 1 -clock clk [get_ports {y z}]
set cell [lindex [get_cells -of_objects [get_nets n]] 0]
if {[get_full_name $cell] eq "i2"} { set cell [lindex [get_cells -of_objects [get_nets n]] 1] }
set_false_path -through $cell
check_eq [get_full_name $cell] u/x
check_eq [get_full_name [get_cells u/x]] u/x
proc ends {port} { llength [find_timing_paths -to [get_ports $port]] }
check_eq "[ends y] [ends z]" {1 0}
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq "[ends y] [ends z]" {1 0}
