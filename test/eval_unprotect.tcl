# -eval -unprotect moves a hier pin clock to drivers, loss names the pin
source test/lib/check.tcl
link_top test/data/flat.v
create_clock -name hc -period 10 [get_pins u1/u2/y]
yosys flatten
log_of opensta_net -sync
set log [log_of opensta_net -eval {yosys select -count t:*} -unprotect]
check_eq [sync_losses $log] 1
check_eq [lost_cmds $log] {{create_clock -name hc -period 10.0000 [get_pins {u1/u2/y}]}}
check_eq [count_lines $log "*lost or changed: applied on the drivers of a hierarchical pin*"] 1
check_eq [count_lines [written_sdc] {create_clock -name hc -period 10.0000 [get_pins {u1/u2/k/Y}]}] 1
