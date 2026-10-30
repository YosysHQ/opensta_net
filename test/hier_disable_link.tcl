# -link after a hierarchical pin disable leaves no pin of the old network in OpenSTA
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
set_disable_timing [get_pins u/a]
yosys opensta_net -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk [get_ports a]
check_eq [llength [find_timing_paths -from [get_ports a]]] 1
