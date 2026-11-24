# a failed build warns, the next sync recovers and keeps the constraints
source test/lib/check.tcl
link_top test/data/simple.v
yosys opensta_net -sdc test/data/simple.sdc
# a connected cell of a type no library knows
yosys chtype -set FOO top/u_inv1
set log [log_of opensta_net]
check_eq [count_lines $log {ERROR: NetView: no port model for cell type FOO (cell u_inv1).}] 1
check_eq [count_lines $log {*cannot rebuild the STA network (*}] 1
# the next call builds again and fails, its query sees no network
check_eq [count_lines [error_of {log_of opensta_net -eval {get_cells *}}] {*No network has been linked.}] 1
yosys chtype -set INV top/u_inv1
check_eq [sync_losses [log_of opensta_net]] 0
check_eq [format %.2f [worst_slack -max]] 8.60
