# a design change without a Monitor event (delete of a wire), then STA
# queries from raw tcl: the pass start ended the epoch, so OpenSTA reads a
# rebuilt network, not freed objects
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -sdc test/data/sync.sdc
yosys delete top/w:n1b
set log [log_of_tcl {
	set ::slack [format %.2f [worst_slack -max]]
	set ::cells [llength [get_cells *]]
}]
check_eq [sync_losses $log] 0
check_eq $slack 8.85
check_eq $cells 10
