# flatten moves a clock on a hierarchical pin to its driver, it stays there through -unprotect
source test/lib/check.tcl
link_top test/data/flat.v
create_clock -name hc -period 10 [get_pins u1/u2/y]
set_max_delay 3 -to [get_pins u1/r/D]
set_false_path -through [get_cells u1/u2/k]
yosys flatten
set log [log_of opensta_net -eval {yosys select -count t:*} -unprotect]
check_eq [lost_cmds $log] {}
check_eq [count_lines [written_sdc] {create_clock -name hc -period 10.0000 [get_pins {u1.u2.k/Y}]}] 1
# after -unprotect the ids of the cells still find the pin
set_false_path -through [get_pins u1.u2.k/Y]
yosys add -wire extra top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [llength [get_property [get_clocks hc] sources]] 1
