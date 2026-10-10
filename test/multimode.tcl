# A sync carries every mode's constraints over, not only the command mode's.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/sync.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
read_sdc -mode mA test/multimode_a.sdc
read_sdc -mode mB test/multimode_b.sdc
set_mode mB
yosys add -wire dummy top
yosys opensta_net -sync
puts "MODE [sta::cmd_mode_name]"
puts "mB [get_full_name [all_clocks]]"
set_mode mA
puts "mA [get_full_name [all_clocks]]"
