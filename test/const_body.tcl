puts "CONST_BEGIN"
create_clock -name clk -period 10 [get_ports clk]
report_checks -to r2/D -format end
report_constant g
puts "net: [get_full_name [get_nets -of_objects [get_pins g/A2]]]"
puts "zero_: [llength [get_nets zero_]]"
puts "CONST_END"
