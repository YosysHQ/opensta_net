# a new top after the link: the next opensta_net call warns and rebuilds the
# network on the new top
source test/lib/check.tcl
link_top test/data/top_a.v a
check_eq [sta::network_leaf_instance_count] 2
yosys read_verilog test/data/top_b.v
yosys hierarchy -top b
check_eq [count_lines [log_of opensta_net] {Warning: opensta_net: design changed since link, rebuilding the STA network and re-applying the constraints.}] 1
check_eq [sta::network_leaf_instance_count] 3
