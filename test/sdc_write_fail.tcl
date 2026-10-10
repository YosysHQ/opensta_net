# A sync that cannot write the constraints out keeps the view stale (STA
# commands refuse) instead of ending the run. The next sync recovers them.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -period 10 -name clk [get_ports clk]
rename write_sdc ysta_test_write_sdc
proc write_sdc {args} { error "no write" }
yosys add -wire dummy top
puts "SYNC [catch {yosys opensta_net -sync}] [sta::network_is_linked]"
rename write_sdc {}
rename ysta_test_write_sdc write_sdc
yosys opensta_net -sync
puts "CLOCKS [llength [all_clocks]]"
