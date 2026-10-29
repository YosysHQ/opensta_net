# a constraint that no longer resolves does not drop the ones after it: only
# the two u_e constraints are lost
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -sdc test/data/sync_cmds.sdc
yosys delete top/u_e
set log [log_of opensta_net -sync]
check_eq [sync_losses $log] 2
check_eq [lost_cmds $log] {{create_generated_clock -name g -source [get_ports {clk}] -divide_by 2 [get_pins {u_e/Y}]} {set_disable_timing -from {A} -to {Y} [get_cells {u_e}]}}
set sdc [written_sdc]
check_eq [count_lines $sdc {set_input_delay 1.0000 -clock [get_clocks {clk}] -add_delay [get_ports {in}]}] 1
check_eq [count_lines $sdc {set_output_delay 1.0000 -clock [get_clocks {clk}] -add_delay [get_ports {out}]}] 1
check_eq [count_lines $sdc {set_max_delay -through [get_nets {n1b}] 4.0000}] 1
