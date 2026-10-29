# the view orders net pins, instance pins and ports as OpenSTA's reader does
source test/lib/check.tcl
link_top test/data/order.v
set body {
	proc names {objs} { lmap obj $objs {get_full_name $obj} }
	foreach net {m[0] n clk} { puts "net $net: [names [get_pins -of_objects [get_nets $net]]]" }
	foreach inst {u0 u1 u2 s1} { puts "inst $inst: [names [get_pins -of_objects [get_cells $inst]]]" }
	puts "ports: [names [get_ports *]]"
	puts "registers: [names [all_registers]]"
	create_clock -period 10 -name clk [get_ports clk]
	set_input_delay 1 -clock clk [get_ports {in[*]}]
	report_net -digits 6 n
}
check_eq [sta_out $body] [reference "
	read_liberty test/data/simple.lib
	read_verilog test/data/order.v
	link_design top
	$body"]
