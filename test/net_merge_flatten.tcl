# flatten joins a net on a port of its scope with the net above, the ref goes on the old pins of the net
source test/lib/check.tcl
link_top [temp_file {module sub(input b, output y); INV i(.A(b), .Y(y)); endmodule
module top(input a, output y1, o3); wire p; INV g(.A(a), .Y(p)); sub u1(.b(p), .y(y1)); INV h(.A(p), .Y(o3)); endmodule}]
create_clock -name v -period 10
set_input_delay 1 -clock v [get_ports a]
set_output_delay 1 -clock v [get_ports {y1 o3}]
set_false_path -through [get_nets u1/b]
yosys opensta_net -unprotect
yosys flatten
check_eq [lost_cmds [log_of opensta_net -sync]] {}
check_eq [count_lines [written_sdc] {set_false_path -through [get_pins {u1.i/A}]}] 1
# the path beside the old net stays timed
check_eq [llength [find_timing_paths -from [get_ports a] -to [get_ports o3]]] 1
check_eq [llength [find_timing_paths -from [get_ports a] -to [get_ports y1]]] 0
