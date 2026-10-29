# two nets with one sta name: a constraint on the one that the name does not
# find stays on it or is reported, it never moves to the other silently
source test/lib/check.tcl
link_top test/data/shared_names.v
create_clock -name clk -period 10
set_input_delay 1 -clock clk [get_ports a]
set_output_delay 1 -clock clk [get_ports {y z}]
set_false_path -through [get_nets -of_objects [get_pins i3/A]]
proc ends {port} { llength [find_timing_paths -to [get_ports $port]] }
check_eq "[ends y] [ends z]" {0 1}
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq "[ends y] [ends z]" {0 1}
