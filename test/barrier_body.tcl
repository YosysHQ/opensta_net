create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports {in bi*}]
set_output_delay 1 -clock clk [get_ports {out bo*}]
puts "BARRIER_BEGIN"
report_checks -path_delay min_max -fields {fanout cap slew} -digits 3
report_wns -digits 3
report_tns -digits 3
puts "CELLS [lsort [lmap c [get_cells *] {get_full_name $c}]]"
puts "NETS [lsort [lmap n [get_nets *] {get_full_name $n}]]"
puts "LOADS [lsort [lmap p [get_pins -of_objects [get_nets n1]] {get_full_name $p}]]"
puts "BARRIER_END"
