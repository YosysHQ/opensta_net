# write_sdc failing on a later mode keeps no text of the earlier modes, the old capture stays
source test/lib/check.tcl
link_top test/data/simple.v
set_mode mA
set_max_delay 9 -through [get_pins u_inv1/Y]
set log [log_of opensta_net -eval {
	set_max_delay 5 -through [get_pins u_inv1/Y]
	set_mode mB
	fail_capture mB
}]
check_eq [count_lines $log {Warning: opensta_net: write_sdc failed (no write), the last capture stays, a rebuild before the next capture drops the newer changes.}] 1
yosys add -wire w top
check_eq [sync_losses [log_of opensta_net -sync]] 0
set_mode mA
set sdc [written_sdc]
check_eq [count_lines $sdc {set_max_delay -through [get_pins {u_inv1/Y}] 9.0000}] 1
check_eq [count_lines $sdc {set_max_delay *}] 1
