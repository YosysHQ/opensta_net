# OpenSTA keeps the resistance of a net that -unprotect dropped, write_sdc after -link leaves out its line
source test/lib/check.tcl
link_top test/data/simple.v
set_resistance 5 [get_nets n1]
yosys opensta_net -unprotect
yosys opensta_net -link
check_eq [count_lines [written_sdc] {*__ysta_stale*}] 0
