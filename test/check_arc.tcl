# a pass replaces a register, its disabled check arc is the check edge of the new register
source test/lib/check.tcl
yosys read_verilog [temp_file {module top(input clk, input a, output reg q); always @(posedge clk) q <= a; endmodule}]
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk [get_ports a]
set reg [get_cells -of_objects [get_pins -of_objects [get_nets q] -filter "direction==output"]]
set_disable_timing -from C -to D $reg
yosys techmap -map [temp_file {(* techmap_celltype = "$_DFF_P_" *)
module dff_split(input D, input C, output Q);
  wire cn;
  \$_NOT_ inv(.A(C), .Y(cn));
  \$_DFF_N_ ff(.D(D), .C(cn), .Q(Q));
endmodule}]
check_eq [sync_losses [log_of opensta_net]] 0
check_eq [llength [find_timing_paths -from [get_ports a]]] 0
# the edge is in the SDC, so it stays over the next sync
yosys add -wire w top
check_eq [sync_losses [log_of opensta_net]] 0
check_eq [llength [find_timing_paths -from [get_ports a]]] 0
