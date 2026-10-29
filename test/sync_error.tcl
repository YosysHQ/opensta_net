# a rebuild that ends in a command error (a connected cell of a type that no
# library knows) does not stop the syncs after the next -link
source test/lib/check.tcl
link_top test/data/simple.v
yosys chtype -set FOO top/u_inv1
set log [error_of {log_of opensta_net -sync}]
check_eq [count_lines $log {ERROR: NetView: no port model for cell type FOO (cell u_inv1).}] 1
yosys chtype -set INV top/u_inv1
yosys opensta_net -link
yosys opensta_net -sdc test/data/simple.sdc
yosys cd top
yosys rename u_inv1 u_x
yosys cd
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [format %.2f [worst_slack -max]] 8.60
