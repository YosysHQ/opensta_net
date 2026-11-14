# write_sdc failing on a later mode keeps no text, a loss counts once
source test/lib/check.tcl
link_top test/data/simple.v
set_mode mA
set_max_delay 5 -through [get_pins u_inv1/Y]
set_mode mB
rename write_sdc ysta_test_write_sdc
proc ::write_sdc {args} {
	if {"mB" in $args} { error "no write" }
	ysta_test_write_sdc {*}$args
}
yosys delete top/u_inv1
set log [log_of opensta_net -sync]
check_eq [sync_losses $log] 1
check_eq [lost_cmds $log] [list {set_max_delay 5 -through [get_pins u_inv1/Y]}]
