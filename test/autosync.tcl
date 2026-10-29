# with -autosync each yosys command from tcl syncs at its end, constraints
# included
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -autosync
yosys opensta_net -sdc test/data/sync_ports.sdc
check_eq [format %.2f [worst_slack -max]] 8.60
check_eq [sync_losses [log_of_tcl {yosys opt_merge -share_all}]] 0
check_eq [sync_losses [log_of_tcl {yosys opt_clean -purge}]] 0
check_eq [format %.2f [worst_slack -max]] 8.60
check_eq [llength [get_cells *]] 6
