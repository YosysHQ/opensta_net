# -unprotect undoes a hierarchical pin disable while its pins live, so the next -link times again
source test/lib/check.tcl
link_top test/data/flat.v
set_disable_timing [get_pins u1/u2/a]
yosys opensta_net -unprotect
yosys opensta_net -link
check_eq [count_lines [sta_out {report_checks -unconstrained -format end}] {u1/r/D (DFF)*}] 1
