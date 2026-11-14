# flatten dissolves $a and $b, their kept scopes s are twins and the ref below one of them is lost
# the cell out of $t has the id and no scope name, it is not the cell of the ref
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/twin_scope.v
yosys hierarchy -top top
yosys cd top; yosys rename pa {$a}; yosys rename pb {$b}; yosys rename t {$t}; yosys cd
yosys opensta_net -liberty test/data/simple.lib -link
set_disable_timing [get_cells {$a/s/c}]
yosys opensta_net -unprotect
yosys flatten -noscopeinfo
check_eq [sync_losses [log_of opensta_net -sync]] 1
check_eq [count_lines [written_sdc] {set_disable_timing *}] 0
