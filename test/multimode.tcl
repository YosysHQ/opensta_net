# a sync carries the constraints of each mode, not only of the command mode,
# and finds a clock name of a mode that is not the current one
source test/lib/check.tcl
link_top test/data/sync.v
read_sdc -mode mA test/data/multimode_a.sdc
read_sdc -mode mB test/data/multimode_b.sdc
set_mode mB
create_clock -name {clk C} -period 4
set_input_delay 1 -clock [get_clocks {{clk C}}] [get_ports in]
set_mode mA
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [sta::cmd_mode_name] mA
check_eq [get_full_name [all_clocks]] clkA
set_mode mB
check_eq [lsort [lmap clock [all_clocks] {get_name $clock}]] {{clk C} clkB}
