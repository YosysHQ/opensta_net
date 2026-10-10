# clear_network is refused: it would free the libraries under the view.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
if {[catch {sta::clear_network} msg]} { puts "CAUGHT $msg" }
create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
puts "SLACK [format %.2f [worst_slack -max]]"
