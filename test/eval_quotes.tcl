# -eval "..." in a .ys line drops the quotes (the yosys command with one
# string tokenizes it as a .ys line)
source test/lib/check.tcl
link_top test/data/sync.v
yosys {opensta_net -eval "set ::answer [expr {6 * 7}]"}
check_eq $answer 42
