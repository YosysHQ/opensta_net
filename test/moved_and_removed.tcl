# -to behind new logic plus a removed pin names the removed pin once
source test/lib/check.tcl
yosys read_verilog test/data/moved_pins.v
yosys hierarchy -top top
yosys proc
yosys opt_dff
yosys techmap
yosys rename -wire -suffix _reg {t:$_DFF*}
yosys opensta_net -liberty test/data/moved_pins.lib
yosys opensta_net -liberty $nangate -link
set_false_path -to [get_pins {re_reg/D s_reg/D}]
yosys delete top/s_reg
# dfflibmap puts a mux before D of re
yosys dfflibmap -liberty $nangate
yosys abc -liberty $nangate
check_eq [count_lines [log_of opensta_net -sync] {*lost or changed: applied without s_reg/D}] 1
