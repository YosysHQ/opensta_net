# passes move pins behind new logic: the sync keeps the edge of a -from, the -to endpoints, arc ports and a timing edge
set lib deps/OpenSTA/test/nangate45/Nangate45_typ.lib
yosys read_verilog test/moved_pins.v
yosys hierarchy -top top
yosys proc
yosys opt_dff
yosys techmap
yosys rename -wire -suffix _reg {t:$_DFF*}
yosys opensta_net -liberty test/moved_pins.lib
yosys opensta_net -liberty $lib -link
create_clock -name clk -period 10 [get_ports clk]
set_false_path -rise_from [get_pins rn_reg/C]
set_false_path -to [get_pins {re_reg/D s_reg/D}]
set_max_delay 9 -from [get_pins rn_reg/C]
set_disable_timing -from C -to Q [get_cells s_reg]
set_disable_timing [get_timing_edges -from [get_pins rn_reg/C] -to [get_pins rn_reg/Q]]
yosys dfflibmap -liberty $lib
yosys abc -liberty $lib
yosys insbuf -buf BUF_X1 A Z
yosys opensta_net -sync
write_sdc -no_timestamp test/moved_pins_out.sdc
