# OpenSTA keeps the net resistances and voltages of earlier networks
source test/lib/check.tcl
link_top test/data/simple.v
set_resistance 10 [get_nets n1]
set_voltage -object_list [get_nets n1] 1.1
yosys add -wire dummy1 top
check_eq [sync_losses [log_of opensta_net -sync]] 0
yosys add -wire dummy2 top
check_eq [sync_losses [log_of opensta_net -sync]] 0
set sdc [written_sdc]
check_eq [count_lines $sdc {set_resistance 10.0000 [get_nets {n1}]}] 1
check_eq [count_lines $sdc {set_voltage -object_list n1 1.100}] 1
