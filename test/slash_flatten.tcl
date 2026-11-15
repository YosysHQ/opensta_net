# flatten dissolves u/q while u lives, the ref follows the flat cell
source test/lib/check.tcl
link_top test/data/slash.v
set_disable_timing [get_cells {u\/q/b/j}]
yosys opensta_net -unprotect
yosys setattr -set keep_hierarchy 1 top/u
yosys flatten
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [count_lines [written_sdc] {set_disable_timing*}] 1
