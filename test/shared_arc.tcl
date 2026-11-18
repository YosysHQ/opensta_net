# new logic has no edge with just the paths of a disabled arc, the command is lost, the other arcs keep their paths
source test/lib/check.tcl
link_top [temp_file {module top(input a, input b, output s, output co);
  HA_X1 ha(.A(a), .B(b), .S(s), .CO(co));
endmodule}] top $nangate
set_disable_timing -from A -to S [get_cells ha]
# the nand is on the paths from A and B to S and CO
yosys techmap -map [temp_file {module HA_X1(input A, input B, output CO, output S);
  wire n, m;
  NAND2_X1 both(.A1(A), .A2(B), .ZN(n));
  INV_X1 carry(.A(n), .ZN(CO));
  OR2_X1 any(.A1(A), .A2(B), .ZN(m));
  AND2_X1 sum(.A1(m), .A2(n), .ZN(S));
endmodule}]
set log [log_of opensta_net -sync]
check_eq [llength [find_timing_paths -from [get_ports a] -to [get_ports co] -unconstrained]] 1
check_eq [lost_cmds $log] {{set_disable_timing -from {A} -to {S} [get_cells {ha}]}}
