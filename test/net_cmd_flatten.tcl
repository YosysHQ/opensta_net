# a net on a port of a scope that flatten dissolves is leaf pins now, a net-only command on it is lost
source test/lib/check.tcl
link_top test/data/flat.v
set_load 0.1 [get_nets u1/u2/y]
set_resistance 5 [get_nets u1/u2/y]
set_timing_derate -net_delay -early 0.9 [get_nets u1/u2/y]
set_voltage -object_list [get_nets u1/u2/y] 1.1
yosys flatten
check_eq [count_lines [log_of opensta_net -sync] "*lost or changed: no object found for u1/u2/y"] 4
