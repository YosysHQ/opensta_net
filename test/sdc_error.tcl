# A broken SDC is a catchable command error. The session goes on, the
# constraints before the error stay, and warnings name the right line.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/sync.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
if {[catch {yosys opensta_net -sdc test/sdc_error.sdc} msg]} { puts "CAUGHT" }
puts "CLOCKS [llength [all_clocks]]"
