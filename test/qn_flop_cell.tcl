# dfflibmap on a QN-only flop keeps the id and puts an inverter after QN, the ref stays the flop
source test/lib/check.tcl
set qn_lib [temp_file [string map {{pin (Q)} {pin (QN)} {"IQ"} {"IQN"}} [read_file test/data/simple.lib]]]
yosys read_liberty -lib $qn_lib
yosys read_verilog [temp_file {module top(input clk, input d, output q);
  reg r;
  always @(posedge clk) r <= d;
  assign q = r;
endmodule}]
yosys hierarchy -top top
yosys proc
yosys techmap
yosys rename -wire -suffix _reg {t:$_DFF*}
yosys opensta_net -liberty test/data/moved_pins.lib
yosys opensta_net -liberty $qn_lib -link
set_disable_timing [get_cells r_reg]
yosys dfflibmap -liberty $qn_lib
check_eq [lost_cmds [log_of opensta_net]] {}
check_eq [count_lines [written_sdc] {set_disable_timing [get_cells {r_reg}]}] 1
