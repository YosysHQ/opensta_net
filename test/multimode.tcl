# A sync carries every mode's constraints over, not only the command mode's,
# and finds a clock name of a mode that is not the current one
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/sync.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
read_sdc -mode mA test/multimode_a.sdc
read_sdc -mode mB test/multimode_b.sdc
set_mode mB
create_clock -name {clk C} -period 4
set_input_delay 1 -clock [get_clocks {{clk C}}] [get_ports in]
set_mode mA
yosys add -wire dummy top
yosys opensta_net -sync
puts "MODE [sta::cmd_mode_name]"
puts "mA [get_full_name [all_clocks]]"
set_mode mB
puts "mB [lsort [lmap clock [all_clocks] {get_name $clock}]]"
