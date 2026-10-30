# a disable on a pin got before a pass leaves no pin of the old network in OpenSTA
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1 -clock clk [get_ports a]
set pin [get_pins u/a]
yosys add -wire dummy top
catch {set_disable_timing $pin}
yosys opensta_net -sync
# a pin of the old network fails the query
find_timing_paths -from [get_ports a]
