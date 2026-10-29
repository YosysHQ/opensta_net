# OpenSTA commands that edit the network, clear the constraints or free the
# sta are refused. The network and the constraints stay
source test/lib/check.tcl
link_top test/data/simple.v
set edit "network edits are not supported, edit the design in yosys."
check_eq [error_of {read_verilog test/data/simple.v}] "Error: 2929 read_verilog: $edit"
check_eq [error_of {sta::clear_network}] "Error: 2929 clear_network: $edit"
check_eq [error_of {[sta::find_instance u_inv0] set_attribute k v}] "Error: 2929 set_attribute: $edit"
yosys opensta_net -sdc test/data/simple.sdc
check_eq [format %.2f [worst_slack -max]] 8.60
# with constraints
check_eq [error_of {read_verilog test/data/simple.v}] "Error: 2929 read_verilog: $edit"
check_eq [llength [all_clocks]] 1
check_eq [error_of {sta::clear_network}] "Error: 2929 clear_network: $edit"
check_eq [llength [all_clocks]] 1
check_eq [error_of {sta::network_changed}] "Error: 2929 network_changed: it would clear the constraints, opensta_net -sync rebuilds the network."
check_eq [llength [all_clocks]] 1
check_eq [error_of {sta::delete_all_memory}] "Error: 2929 delete_all_memory: the STA instance belongs to opensta_net."
check_eq [llength [all_clocks]] 1
yosys opt
check_eq [format %.2f [worst_slack -max]] 8.60
