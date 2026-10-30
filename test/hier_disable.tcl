# OpenSTA keeps a hierarchical pin disable as wire edges in a set that orders pins by id: a rebuild must not leave the old pins in it, the disable stays
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk [get_ports a]
set_disable_timing [get_pins u/a]
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [llength [find_timing_paths -from [get_ports a]]] 0
yosys add -wire dummy2 top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [llength [find_timing_paths -from [get_ports a]]] 0
