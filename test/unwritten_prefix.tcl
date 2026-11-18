# an abbreviated option that write_sdc does not write is kept through a rebuild
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -name clk -period 10 [get_ports clk]
set_max_delay -pro -from [get_ports in] -to [get_pins u_dff0/D] 3
set_clock_latency -source 1 [get_clocks clk]
set probe [get_property [lindex [find_timing_paths -from [get_ports in] -to [get_pins u_dff0/D]] 0] slack]
yosys add -wire extra top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [get_property [lindex [find_timing_paths -from [get_ports in] -to [get_pins u_dff0/D]] 0] slack] $probe
check_eq [llength $::ysta::kept] 1
