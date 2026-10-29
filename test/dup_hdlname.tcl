# two cells with one hdlname keep their own names, none hides the other
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/simple.v
yosys hierarchy -top top
yosys setattr -set hdlname {"dup"} c:u_inv1 c:u_inv2
yosys opensta_net -liberty test/data/simple.lib -link
check_eq [lsort [lmap cell [get_cells *] {get_full_name $cell}]] {u_dff0 u_inv0 u_inv1 u_inv2}
create_clock -name clk -period 10 [get_ports clk]
set_output_delay 1 -clock clk [get_ports out]
check_eq [count_lines [sta_out {report_checks -to [get_ports out] -format end}] {out (output) * 9.00 * 0.40 * 8.60 (MET)}] 1
