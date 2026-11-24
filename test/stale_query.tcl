# a query after a design change, its call syncs first
# OpenSTA then reads a rebuilt network, not freed objects
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -sdc test/data/sync.sdc
yosys delete top/w:n1b
set log [log_of opensta_net -eval {list [format %.2f [worst_slack -max]] [llength [get_cells *]]}]
check_eq [sync_losses $log] 0
check_eq [count_lines $log {8.85 10}] 1
