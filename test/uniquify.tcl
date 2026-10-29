# uniquified copies through OpenSTA's write_verilog: equal copies are one
# module, a copy edited in yosys gets its own. OpenSTA's reader of each
# output reports as the view
source test/lib/check.tcl
yosys read_liberty -lib $nangate
yosys read_verilog test/data/uniq.v
yosys hierarchy -top top
yosys uniquify
yosys opensta_net -liberty $nangate -link
set body {
	puts "cells: [get_property [get_cells u1] ref_name] [get_property [get_cells u2] ref_name]"
	puts "u1/b: [get_property [get_cells u1/b] ref_name] u2/b: [get_property [get_cells u2/b] ref_name]"
	report_checks -unconstrained -to o1
	report_checks -unconstrained -to o2
}
set same [sta_out $body]
write_verilog test/out/uniq_same.v
yosys chtype -set CLKBUF_X1 top.u2/b
set diff [sta_out $body]
write_verilog test/out/uniq_diff.v
check_eq [count_lines [read_file test/out/uniq_same.v] {module *}] 2
check_eq [count_lines [read_file test/out/uniq_diff.v] {module *}] 3
check_eq $same [reference "
	read_liberty $nangate
	read_verilog test/out/uniq_same.v
	link_design top
	$body"]
check_eq $diff [reference "
	read_liberty $nangate
	read_verilog test/out/uniq_diff.v
	link_design top
	$body"]
