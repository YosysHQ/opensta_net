puts "ORDER_BEGIN"
proc names {objs} {
	set result {}
	foreach obj $objs { lappend result [get_full_name $obj] }
	return $result
}
foreach net {m[0] n clk} { puts "net $net: [names [get_pins -of_objects [get_nets $net]]]" }
foreach inst {u0 u1 u2 s1} { puts "inst $inst: [names [get_pins -of_objects [get_cells $inst]]]" }
puts "ports: [names [get_ports *]]"
puts "registers: [names [all_registers]]"
create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports {in[*]}]
report_net -digits 6 n
puts "ORDER_END"
