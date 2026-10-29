# opt_merge and opt_clean on a linked design give the netlist and timing of a
# link after the same passes
source test/lib/check.tcl
link_top test/data/opt.v
yosys opensta_net -sdc test/data/sync_ports.sdc
check_eq [sta::network_leaf_instance_count] 7
yosys opt_merge -share_all
yosys opt_clean
yosys opensta_net -sdc test/data/sync_ports.sdc
check_eq [sta::network_leaf_instance_count] 4
check_eq [format %.4f [worst_slack -max]] 8.6000

yosys design -reset
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/opt.v
yosys hierarchy -top top
yosys opt_merge -share_all
yosys opt_clean
yosys opensta_net -link
yosys opensta_net -sdc test/data/sync_ports.sdc
check_eq [sta::network_leaf_instance_count] 4
check_eq [format %.4f [worst_slack -max]] 8.6000
