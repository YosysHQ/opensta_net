# a ref below the private scope $a gives $a keep_hierarchy, so flatten keeps it
# $b dissolves, its kept scope s is a twin of the one in $a
# the cell out of $t has the id and no scope name, it is not the cell of the ref
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/twin_scope.v
yosys hierarchy -top top
yosys cd top; yosys rename pa {$a}; yosys rename pb {$b}; yosys rename t {$t}; yosys cd
yosys opensta_net -liberty test/data/simple.lib -link
set_disable_timing [get_cells {$a/s/c}]
yosys flatten -noscopeinfo
check_eq [sync_losses [log_of opensta_net]] 0
check_eq [count_lines [written_sdc] {set_disable_timing [get_cells {$a/s/c}]}] 1
