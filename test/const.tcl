# a constant pin: the view and OpenSTA's reader of the write_verilog output both have the constant net zero_, as OpenSTA's reader of the input
source test/lib/check.tcl
link_top test/data/const.v top $nangate
set body {
	create_clock -name clk -period 10 [get_ports clk]
	report_checks -to r2/D -format end
	report_constant g
	puts "net: [get_full_name [get_nets -of_objects [get_pins g/A2]]]"
	puts "zero_: [llength [get_nets zero_]]"
}
set got [sta_out $body]
check_eq [count_lines $got {zero_: 1}] 1
check_eq $got [reference "
	read_liberty $nangate
	read_verilog test/data/const.v
	link_design top
	$body"]
write_verilog test/out/const.v
check_eq $got [reference "
	read_liberty $nangate
	read_verilog test/out/const.v
	link_design top
	$body"]
