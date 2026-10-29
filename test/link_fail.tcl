# a rebuild that fails (a cell type without liberty cell) does not end the run:
# the network stays unlinked and the next good sync brings the constraints
# back. Once by -sync, once by -autosync
source test/lib/check.tcl
link_top test/data/simple.v
yosys opensta_net -sdc test/data/simple.sdc
check_eq [format %.2f [worst_slack -max]] 8.60
yosys chtype -set {$_NOT_} top/u_inv1
set log [log_of opensta_net -sync]
check_eq [count_lines $log {Warning: opensta_net: cannot rebuild the STA network (2923 internal cell type $_NOT_ has no liberty cell, *), it stays unlinked until a sync succeeds.}] 1
check_eq [sta::network_is_linked] 0
yosys chtype -set INV top/u_inv1
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [format %.2f [worst_slack -max]] 8.60
yosys opensta_net -autosync
yosys chtype -set {$_NOT_} top/u_inv1
check_eq [sta::network_is_linked] 0
yosys chtype -set INV top/u_inv1
check_eq [format %.2f [worst_slack -max]] 8.60
