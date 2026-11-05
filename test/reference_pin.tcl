# a -reference_pin that a pass turns into several pins of one clock keeps one of them
source test/lib/check.tcl
yosys read_verilog test/data/track.v
yosys hierarchy -top top
yosys synth -top top
yosys dfflibmap -liberty $nangate
yosys abc -liberty $nangate
yosys opensta_net -liberty $nangate -link
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1.5 -clock clk -reference_pin [get_pins u1/clk] [get_ports {in[3]}]
yosys flatten
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [count_lines [written_sdc] {set_input_delay 1.5000 -clock * -reference_pin [get_pins {u1/*/CK}] [get_ports {in[3]}]}] 1
