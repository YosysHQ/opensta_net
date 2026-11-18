# -to behind new logic plus a removed pin names the removed pin once
source test/lib/check.tcl
link_moved_pins
set_false_path -to [get_pins {re_reg/D s_reg/D}]
yosys delete top/s_reg
# dfflibmap puts a mux before D of re
yosys dfflibmap -liberty $nangate
yosys abc -liberty $nangate
check_eq [count_lines [log_of opensta_net -sync] {*lost or changed: applied without s_reg/D}] 1
