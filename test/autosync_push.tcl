# -autosync across `design -push` / `-pop`: no top while pushed, so the old
# network stays (unlinked) instead of ending the run.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/sync.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link -autosync
create_clock -period 10 -name clk [get_ports clk]
set_output_delay 1 -clock clk [get_ports out]
yosys design -push
puts "PUSHED linked: [sta::network_is_linked]"
yosys design -pop
puts "POPPED clocks: [llength [all_clocks]]"
report_checks -format end
