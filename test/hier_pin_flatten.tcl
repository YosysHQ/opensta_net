# flatten dissolves the scope of a hierarchical pin, its leaf pins stand in, -unprotect with no sync before keeps them
source test/lib/check.tcl
link_top test/data/flat.v
create_clock -name hc -period 10 [get_pins u1/u2/y]
set_false_path -through [get_pins u1/u2/a]
set_case_analysis 1 [get_pins u1/a]
set_false_path -from [get_pins u1/a]
yosys flatten
# -through, a case value and a clock are the same on the leaf pins, a -from is a changed form and reported once
check_eq [lost_cmds [log_of opensta_net -unprotect]] {{set_false_path -from [get_pins {u1/a}]}}
yosys flatten
check_eq [lost_cmds [log_of opensta_net -sync]] {}
set sdc [written_sdc]
check_eq [count_lines $sdc {create_clock -name hc -period 10.0000 [get_pins {u1.u2.k/Y}]}] 1
check_eq [count_lines $sdc {set_false_path -through [get_pins {u1.u2.j/A}]}] 1
check_eq [count_lines $sdc {set_case_analysis 1 [get_pins {u1.i0/A}]}] 1
