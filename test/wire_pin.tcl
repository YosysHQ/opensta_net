# a clock on a pin of a cell that is a wire now moves to the load pin
source test/lib/check.tcl
yosys read_verilog test/data/wire_pin.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
create_clock -name clk -period 10 [get_ports clk]
set mux_y [get_pins -of_objects [get_cells -filter "ref_name=~*MUX*"] -filter "direction==output"]
create_clock -name g -period 5 $mux_y
set_clock_latency 0.3 $mux_y
set_clock_uncertainty 0.2 $mux_y
set_sense -stop_propagation -clocks [get_clocks clk] $mux_y
set_max_delay 3 -from $mux_y
set_output_delay 1 -clock clk [get_ports q]
yosys opt_expr -mux_undef
set log [log_of opensta_net -sync]
check_eq [sync_losses $log] 0
check_eq [llength [all_clocks]] 2
check_eq [count_lines [written_sdc] {set_clock_latency 0.3000 [get_pins {*/C}]}] 1
