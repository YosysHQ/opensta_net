# a failing -eval is a command error that tcl can catch
source test/lib/check.tcl
link_top test/data/simple.v
set log [error_of {log_of opensta_net -eval no_such_cmd}]
check_eq [count_lines $log {ERROR: OpenSTA tcl error: invalid command name "no_such_cmd"}] 1
# the run goes on
check_eq [sta::network_leaf_instance_count] 4
