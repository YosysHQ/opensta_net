# a -reference_pin that becomes several pins of one clock keeps one
source test/lib/check.tcl
link_track
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1.5 -clock clk -reference_pin [get_pins u1/clk] [get_ports {in[3]}]
yosys flatten
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [count_lines [written_sdc] {set_input_delay 1.5000 -clock * -reference_pin [get_pins {u1/*/CK}] [get_ports {in[3]}]}] 1
