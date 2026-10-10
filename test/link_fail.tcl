# A rebuild that fails (a cell type without liberty cell) does not end the
# run: the network stays unlinked and the next good sync brings the
# constraints back. Once by -sync, once by -autosync (in the Tcl trace).
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
puts "SLACK [format %.2f [worst_slack -max]]"
yosys chtype -set {$_NOT_} top/u_inv1
puts "SYNC [catch {yosys opensta_net -sync}] [sta::network_is_linked]"
yosys chtype -set INV top/u_inv1
yosys opensta_net -sync
puts "SLACK [format %.2f [worst_slack -max]]"
yosys opensta_net -autosync
yosys chtype -set {$_NOT_} top/u_inv1
puts "AUTOSYNC [sta::network_is_linked]"
yosys chtype -set INV top/u_inv1
puts "SLACK [format %.2f [worst_slack -max]]"
