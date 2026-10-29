# get_fanout and get_fanin of an empty list are empty
source test/lib/check.tcl
link_top test/data/simple.v
check_eq [llength [get_fanout -from {}]] 0
check_eq [llength [get_fanout -from {} -only_cells]] 0
check_eq [llength [get_fanin -to {}]] 0
check_eq [llength [get_fanin -to {} -only_cells]] 0
