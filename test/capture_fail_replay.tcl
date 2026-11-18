# a failed capture loses no command, the sync runs the journal again by name after the captured commands
source test/lib/check.tcl
link_top test/data/simple.v
set_mode mA
set_max_delay 9 -through [get_pins u_inv1/Y]
yosys add -wire captured top
# journal order, the last one wins
set_max_delay 7 -through [get_pins u_inv1/Y]
# an empty word is no lost object
set_max_delay 5 -through [get_pins u_inv1/Y] -comment {}
# a plain name beside an object stays
set_false_path -to [list [get_ports out] u_dff0/D]
set_mode mB
check_eq [fail_capture add -wire first top] 0
# the replayed commands stay in the journal for the next failed capture
check_eq [fail_capture add -wire second top] 0
check_eq [count_lines [written_sdc] {set_*}] 0
set_mode mA
check_eq [count_lines [written_sdc] {set_max_delay -through [get_pins {u_inv1/Y}] 5.0000}] 1
check_eq [count_lines [written_sdc] {set_false_path -to [list [get_ports {out}] [get_pins {u_dff0/D}]]}] 1
# the hierarchical name finds the pin after a flatten
yosys design -reset
link_top test/data/flat.v
yosys opensta_net -unprotect
set_max_delay 6 -to [get_pins u1/r/D]
check_eq [fail_capture flatten] 0
check_eq [count_lines [written_sdc] {set_max_delay -to [get_pins {u1.r/D}] 6.0000}] 1
