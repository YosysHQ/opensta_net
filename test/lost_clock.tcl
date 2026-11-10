# a removed clock pin loses its clock commands, a list keeps the rest
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -name c -period 10 [get_pins u_inv1/Y]
create_clock -name m -period 10 [get_ports clk]
set_input_delay -clock c 1 [get_ports in]
set_false_path -from [get_clocks {c m}] -to [get_pins u_dff0/D]
yosys delete top/u_inv1
set log [log_of opensta_net -sync]
check_eq [sync_losses $log] 3
check_eq [count_lines $log "*lost or changed: no object found for c"] 1
check_eq [count_lines $log "*lost or changed: applied without c"] 1
check_eq [count_lines [written_sdc] {set_false_path -from [get_clocks {m}] -to [get_pins {u_dff0/D}]}] 1
