# two nets with one sta name: a constraint on the one that the name does not
# find stays on it or is reported, never moves to the other silently
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/shared_names.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -name clk -period 10
set_input_delay 1 -clock clk [get_ports a]
set_output_delay 1 -clock clk [get_ports {y z}]
set_false_path -through [get_nets -of_objects [get_pins i3/A]]
proc ends {port} { return [llength [find_timing_paths -to [get_ports $port]]] }
puts "BEFORE y [ends y] z [ends z]"
yosys add -wire dummy top
yosys opensta_net -sync
puts "AFTER y [ends y] z [ends z]"
