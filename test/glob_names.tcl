# a sync takes * or backslash in a port or clock name literally
source test/lib/check.tcl
link_top [temp_file {module top(input clk, input \y*z , input yAz, output out); INV u_inv (.A(\y*z ), .Y(out)); endmodule}]
create_clock -name c* -period 10 [get_ports clk]
create_clock -name cX -period 20 -add [get_ports clk]
create_clock -name {a\b} -period 30 -add [get_ports clk]
set_input_delay -clock c* 1 [get_ports -of_objects [get_nets -of_objects [get_pins u_inv/A]]]
set_input_delay -clock {a\b} 2 [get_ports yAz]
set sdc [written_sdc]
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [written_sdc] $sdc
