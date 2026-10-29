# escaped names, bus bits and assign aliases: the view lists and finds them as
# OpenSTA's reader, but for the known differences below
source test/lib/check.tcl
link_top test/data/stress.v stress $nangate
set body {
	puts "--- ports ---"
	foreach p [get_ports *] { puts "port: [get_full_name $p] dir [get_property $p direction]" }
	puts "escaped port: [llength [get_ports {weird/name}]]"
	puts "bracket port: [llength [get_ports {a[3]}]]"
	puts "rev port bits: [llength [get_ports {rev_in[*]}]]"
	puts "--- cells ---"
	foreach c [get_cells *] { puts "cell: [get_full_name $c]" }
	puts "esc inst: [llength [get_cells {esc/inst}]]"
	puts "esc inst pins: [llength [get_pins {esc/inst/*}]]"
	puts "--- nets ---"
	foreach n [get_nets *] { puts "net: [get_full_name $n]" }
	puts "alias1: [llength [get_nets alias1]]"
	puts "alias2: [llength [get_nets alias2]]"
	puts "escnet: [llength [get_nets {x/y}]]"
	create_clock -period 10 -name clk [get_ports clk]
	report_checks -endpoint_path_count 1
}
set got [sta_out $body]
set want [reference "
	read_liberty $nangate
	read_verilog test/data/stress.v
	link_design stress
	$body"]
# known difference 1: a warning names the script and its line, these differ
regsub {Warning 366: \S+ line \d+, } $got {Warning 366: } got
regsub {Warning 366: \S+ line \d+, } $want {Warning 366: } want
# known difference 2: alias1, alias2 and the port weird/name are one net in
# yosys, named by the port and listed in the place of alias1
set want [string map {"net: alias1\nnet: alias2\n" "net: weird/name\n" "net: weird/name\n" ""} $want]
check_eq $got $want
