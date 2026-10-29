# the sync applies again what write_sdc skips or writes raw, the commands after
# the epoch end, user properties and kept exceptions around an unset in order
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -name clk -period 10 [get_ports clk]
create_clock -name {a b} -period 10
set_input_delay 1 -clock clk -network_latency_included [get_ports in]
set_output_delay 1 -clock clk [get_ports out]
set_output_delay 2 -clock [get_clocks {{a b}}] -add_delay [get_ports out]
set_clock_gating_check -setup 0.5 [get_clocks clk]
set_voltage -object_list [get_nets n1] 1.1
define_property -object_type instance -type string tag
define_property -object_type pin -type string tag
define_property -object_type liberty_cell -type string tag
set_property [get_cells u_inv1] tag CELL
set_property [get_pins u_dff0/Q] tag PIN
set_property [get_lib_cells simple/INV] tag LIB
set_max_delay -probe -from [get_ports in] -to [get_pins u_dff0/D] 3
unset_path_exceptions -from [get_ports in] -to [get_pins u_dff0/D]
set_max_delay -probe -from [get_ports in] -to [get_pins u_dff0/D] 6
yosys add -wire dummy top
# after the epoch end
set_clock_uncertainty 0.7 [get_clocks clk]
set_clock_groups -name {g h} -asynchronous -group [get_clocks {{a b}}]
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [kept_cmds] {set_input_delay 1 -clock clk -network_latency_included [get_ports {in}]
set_clock_gating_check -setup 0.5 [get_clocks {clk}]
set_property [get_cells {u_inv1}] tag CELL
set_property [get_pins {u_dff0/Q}] tag PIN
set_max_delay -probe -from [get_ports {in}] -to [get_pins {u_dff0/D}] 3
unset_path_exceptions -from [get_ports {in}] -to [get_pins {u_dff0/D}]
set_max_delay -probe -from [get_ports {in}] -to [get_pins {u_dff0/D}] 6}
check_eq [get_property [get_cells u_inv1] tag] CELL
check_eq [get_property [get_pins u_dff0/Q] tag] PIN
check_eq [get_property [get_lib_cells simple/INV] tag] LIB
check_eq [get_property [lindex [find_timing_paths -from [get_ports in] -to [get_pins u_dff0/D]] 0] slack] 4.150000
check_eq [lsort [lmap clock [all_clocks] {get_name $clock}]] {{a b} clk}
set sdc [written_sdc]
check_eq [count_lines $sdc {set_clock_uncertainty 0.7000 clk}] 1
check_eq [count_lines $sdc {set_voltage -object_list n1 1.100}] 1
# write_sdc writes the names with a space unbraced
check_eq [count_lines $sdc {set_clock_groups -name g h -asynchronous -group [get_clocks {a b}]}] 1
