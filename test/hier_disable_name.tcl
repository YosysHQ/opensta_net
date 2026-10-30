# a hierarchical pin disable by name leaves no pin of the old network in OpenSTA
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk [get_ports a]
set_disable_timing u/a
yosys add -wire dummy top
yosys opensta_net -sync
# a pin of the old network fails the query
find_timing_paths -from [get_ports a]
