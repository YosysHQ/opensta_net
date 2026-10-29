# a pass start whose write_sdc fails keeps the constraints of the last epoch
# end and the journal of the commands since
source test/lib/check.tcl
# no warning that the journal lacks commands
yosys logger -werror {journal lacks}
link_top test/data/simple.v
create_clock -period 10 -name clk [get_ports clk]
set_max_delay 9 -through [get_pins u_inv0/Y]
proc break_write_sdc {} {
	rename write_sdc ysta_test_write_sdc
	proc ::write_sdc {args} { error "no write" }
}
proc fix_write_sdc {} {
	rename write_sdc {}
	rename ysta_test_write_sdc write_sdc
}
break_write_sdc
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [sta::network_is_linked] 1
fix_write_sdc
yosys opensta_net -sync
check_eq [llength [all_clocks]] 1
# a command after a good epoch end, then a failing write: the constraints of
# that epoch end and the command stay
set_input_delay 2 -clock clk [get_ports in]
break_write_sdc
yosys add -wire dummy2 top
fix_write_sdc
yosys opensta_net -sync
check_eq [llength [all_clocks]] 1
set sdc [written_sdc]
check_eq [count_lines $sdc {set_input_delay 2.0000 -clock [get_clocks {clk}] -add_delay [get_ports {in}]}] 1
check_eq [count_lines $sdc {set_max_delay -through [get_pins {u_inv0/Y}] 9.0000}] 1
