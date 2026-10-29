# a linked design has the instances, pins and nets of the netlist, and times
source test/lib/check.tcl
link_top test/data/simple.v
check_eq [sta::network_leaf_instance_count] 4
check_eq [sta::network_leaf_pin_count] 11
check_eq [sta::network_net_count] 6
yosys opensta_net -sdc test/data/simple.sdc
check_eq [count_lines [sta_out report_checks] {*8.60   slack (MET)}] 1
