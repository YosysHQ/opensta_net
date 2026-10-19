yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
set p [get_pins u_inv2/Y]
yosys opensta_net -link
if {[catch {get_full_name $p} msg]} { puts "CAUGHT $msg" } else { puts "TOOK $msg" }
puts "FRESH [get_full_name [get_pins u_inv2/Y]]"
