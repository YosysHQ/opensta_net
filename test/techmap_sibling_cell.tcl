# a map keeps the id on one new cell, a cell disable covers the siblings, a wire output warns
source test/lib/check.tcl
# links a half adder, disables it, maps it with the body, returns the sync log
proc map_ha {body} {
	yosys design -reset
	link_top [temp_file {module top(input a, input b, output s, output co);
  HA_X1 ha(.A(a), .B(b), .S(s), .CO(co));
endmodule}] top $::nangate
	set_disable_timing [get_cells ha]
	yosys techmap -map [temp_file "module HA_X1(input A, input B, output CO, output S);$body endmodule"]
	return [log_of opensta_net]
}
set log [map_ha {
  wire n, m;
  NAND2_X1 both(.A1(A), .A2(B), .ZN(n));
  INV_X1 carry(.A(n), .ZN(CO));
  OR2_X1 any(.A1(A), .A2(B), .ZN(m));
  AND2_X1 _TECHMAP_REPLACE_(.A1(m), .A2(n), .ZN(S));
}]
check_eq [lost_cmds $log] {}
check_eq [llength [find_timing_paths -from [get_ports a] -to [get_ports co] -unconstrained]] 0
set log [map_ha {
  assign CO = A;
  AND2_X1 _TECHMAP_REPLACE_(.A1(A), .A2(B), .ZN(S));
}]
check_eq [lost_cmds $log] {{set_disable_timing [get_cells {ha}]}}
