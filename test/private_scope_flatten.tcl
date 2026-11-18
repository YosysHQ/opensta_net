# after -unprotect flatten dissolves a scope with a private name, its cells keep no scope name
# the one cell with the id and the names below the scope is the cell of the ref, not its copy out of u2
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/nest.v
yosys hierarchy -top top
yosys cd top; yosys rename u1 {$u1}; yosys cd
yosys opensta_net -liberty test/data/simple.lib -link
set_disable_timing [get_cells {$u1/b/j}]
set_max_delay 1 -through [get_nets {$u1/b/n}]
set_false_path -through [get_pins {$u1/c/k/A}]
yosys opensta_net -unprotect
yosys flatten -noscopeinfo
check_eq [lost_cmds [log_of opensta_net -sync]] {}
set sdc [written_sdc]
check_eq [count_lines $sdc {set_disable_timing [get_cells {$u1.b.j}]}] 1
check_eq [count_lines $sdc {set_max_delay -through [get_nets {$u1.b.n}] 1.0000}] 1
check_eq [count_lines $sdc {set_false_path -through [get_pins {$u1.c.k/A}]}] 1
check_eq [count_lines $sdc {set_*}] 3
