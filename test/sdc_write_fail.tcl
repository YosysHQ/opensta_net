# a pass start whose write_sdc fails keeps the constraints of the last epoch
# end and the journal of the commands since
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -period 10 -name clk [get_ports clk]
set_max_delay 9 -through [get_pins u_inv0/Y]
rename write_sdc ysta_test_write_sdc
proc write_sdc {args} { error "no write" }
yosys add -wire dummy top
puts "SYNC [catch {yosys opensta_net -sync}] [sta::network_is_linked]"
rename write_sdc {}
rename ysta_test_write_sdc write_sdc
yosys opensta_net -sync
puts "CLOCKS [llength [all_clocks]]"
# a command after a good epoch end, then a failing write: the constraints of
# that epoch end and the command stay
set_input_delay 2 -clock clk [get_ports in]
rename write_sdc ysta_test_write_sdc
proc write_sdc {args} { error "no write" }
yosys add -wire dummy2 top
rename write_sdc {}
rename ysta_test_write_sdc write_sdc
yosys opensta_net -sync
set file [file tempfile path]
close $file
write_sdc -no_timestamp $path
set file [open $path]
set text [read $file]
close $file
file delete $path
puts "KEPT [llength [all_clocks]] [regexp -all {set_input_delay 2} $text] [regexp -all {u_inv0/Y} $text]"
