# A failing -eval is a command error, catchable from Tcl.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
if {[catch {yosys opensta_net -eval no_such_cmd}]} { puts "CAUGHT" }
puts "ALIVE"
