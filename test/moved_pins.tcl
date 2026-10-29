# passes move pins behind new logic: the sync keeps the edge of a -from, the
# -to endpoints, arc ports and a timing edge
source test/lib/check.tcl
yosys read_verilog test/data/moved_pins.v
yosys hierarchy -top top
yosys proc
yosys opt_dff
yosys techmap
yosys rename -wire -suffix _reg {t:$_DFF*}
yosys opensta_net -liberty test/data/moved_pins.lib
yosys opensta_net -liberty $nangate -link
create_clock -name clk -period 10 [get_ports clk]
set_false_path -rise_from [get_pins rn_reg/C]
set_false_path -to [get_pins {re_reg/D s_reg/D}]
set_max_delay 9 -from [get_pins rn_reg/C]
set_disable_timing -from C -to Q [get_cells s_reg]
set_disable_timing [get_timing_edges -from [get_pins rn_reg/C] -to [get_pins rn_reg/Q]]
# dfflibmap puts an inverter before the clock of rn and a mux before D of re
yosys dfflibmap -liberty $nangate
yosys abc -liberty $nangate
yosys insbuf -buf BUF_X1 A Z
check_eq [sync_losses [log_of opensta_net -sync]] 0
set sdc [written_sdc]
check_eq [count_lines $sdc {set_false_path -fall_from [get_pins {rn_reg/CK}]}] 1
check_eq [count_lines $sdc {set_false_path -through [list [get_pins {*/B}] [get_pins {s_reg/D}]] -to [list [get_pins {re_reg/D}] [get_pins {s_reg/D}]]}] 1
check_eq [count_lines $sdc {set_max_delay -from [get_pins {rn_reg/CK}] 9.0000}] 1
check_eq [count_lines $sdc {set_disable_timing -from {CK} -to {Q} [get_cells {s_reg}]}] 1
check_eq [count_lines $sdc {set_disable_timing [get_timing_edges -from [get_pins {*/Z}] -to [get_pins {*/A}]]}] 1
