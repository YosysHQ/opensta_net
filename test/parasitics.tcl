# a rebuild drops the parasitics and says so
source test/lib/check.tcl
link_top test/data/sync.v
sta::set_pi_model u_a/Y 0.01 1 0.01
yosys add -wire dummy top
set log [log_of opensta_net -sync]
check_eq [count_lines $log {Warning: opensta_net: the rebuild drops the loaded parasitics, read them again.}] 1
check_eq [sync_losses $log] 0
