# a module used twice links without uniquify, as OpenSTA's reader of the input
source test/lib/check.tcl
link_top test/data/uniq.v top $nangate
set body {
	puts "cells: [get_property [get_cells u1] ref_name] [get_property [get_cells u2] ref_name]"
	puts "u1/b: [get_property [get_cells u1/b] ref_name] u2/b: [get_property [get_cells u2/b] ref_name]"
	report_checks -unconstrained -to o1
	report_checks -unconstrained -to o2
}
check_eq [sta_out $body] [reference "
	read_liberty $nangate
	read_verilog test/data/uniq.v
	link_design top
	$body"]
