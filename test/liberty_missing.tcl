# a liberty file that cannot be read is a command error, not an abort
source test/lib/check.tcl
set log [error_of {log_of opensta_net -liberty test/data/no_such.lib}]
check_eq [count_lines $log {ERROR: OpenSTA: cannot read liberty file test/data/no_such.lib: cannot read file test/data/no_such.lib.}] 1
# the run goes on
link_top test/data/simple.v
check_eq [sta::network_leaf_instance_count] 4
