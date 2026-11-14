# a wire with the name of a constant net makes the link warn
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/zero_name.v
yosys hierarchy -top top
set log [log_of opensta_net -liberty test/data/simple.lib -link]
check_eq [count_lines $log {Warning: opensta_net: 1 wire(s) have the name of a constant net (one_ or zero_). A getter by that name finds the constant net.}] 1
# the constant net takes the name
check_eq [names [get_pins -of_objects [get_nets zero_]]] {i2/A}
