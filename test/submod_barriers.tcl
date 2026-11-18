# submod moves the barriers of a driver pin, of its net and of the load into a new scope below the driver
# the walk from the cut wire goes up through the port to the driver, the net is the part in the new scope
source test/lib/check.tcl
link_top test/data/simple.v
set_min_delay 0.1 -through [get_pins u_inv1/Y]
set_max_delay 5 -through [get_nets n1]
set_false_path -through [get_pins u_inv2/A]
yosys submod -name grp {top/t:$barrier}
check_eq [lost_cmds [log_of opensta_net -sync]] {}
set sdc [written_sdc]
check_eq [count_lines $sdc {set_min_delay -through [get_pins {u_inv1/Y}] 0.1000}] 1
check_eq [count_lines $sdc {set_max_delay -through [get_nets {grp/n2}] 5.0000}] 1
check_eq [count_lines $sdc {set_false_path -through [get_pins {u_inv2/A}]}] 1
