# set_attribute on an instance is refused: the instance is not OpenSTA's.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
if {[catch {[sta::find_instance u_inv0] set_attribute k v} msg]} { puts "CAUGHT $msg" }
puts "SLACK [format %.2f [worst_slack -max]]"
