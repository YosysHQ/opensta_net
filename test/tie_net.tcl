# a public wire tied to a constant stays findable by its own name
source test/lib/check.tcl
link_top test/data/tie.v
check_eq [llength [get_nets tie]] 1
