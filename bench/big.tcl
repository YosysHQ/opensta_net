create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [all_inputs -no_clocks]
puts "insts: [sta::network_leaf_instance_count] pins: [sta::network_leaf_pin_count] nets: [sta::network_net_count]"
set t0 [clock milliseconds]
puts "cells -hier *: [llength [get_cells -hierarchical *]]"
puts "pins -hier *: [llength [get_pins -hierarchical *]]"
set t1 [clock milliseconds]
puts "query ms: [expr {$t1 - $t0}]"
report_checks -path_delay max -endpoint_path_count 1 > /dev/null
set t2 [clock milliseconds]
puts "first report_checks ms: [expr {$t2 - $t1}]"
report_tns
report_wns
set t3 [clock milliseconds]
puts "tns/wns ms: [expr {$t3 - $t2}]"
