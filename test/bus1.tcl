# ports declared [0:0] are buses of one bit, as in OpenSTA's reader
source test/lib/check.tcl
yosys read_verilog test/data/bus1.v
yosys hierarchy -top top
yosys opensta_net -liberty test/data/simple.lib -link
set body {
	puts "ports [lsort [lmap o [get_ports *] {get_full_name $o}]]"
	puts "hier [lsort [lmap o [get_pins h/*] {get_full_name $o}]]"
	puts "leaf [lsort [lmap o [get_pins h/u/*] {get_full_name $o}]]"
}
check_eq [sta_out $body] [reference "
	read_liberty test/data/simple.lib
	read_verilog test/data/bus1.v
	link_design top
	$body"]
