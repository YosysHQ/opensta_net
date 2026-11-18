# STA queries from raw Tcl after a design change
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -sdc test/data/sync.sdc
yosys delete top/w:n1b
# the delete pass ended the epoch at its start
# OpenSTA then reads a rebuilt network, not freed objects
set log [log_of_tcl {
	set ::slack [format %.2f [worst_slack -max]]
	set ::cells [llength [get_cells *]]
}]
check_eq [sync_losses $log] 0
check_eq $slack 8.85
check_eq $cells 10
