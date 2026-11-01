# two nets with one sta name: the link warns
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/shared_names.v
yosys hierarchy -top top
set log [log_of opensta_net -liberty test/data/simple.lib -link]
check_eq [count_lines $log {Warning: opensta_net: 1 net(s) have the name of another net in their scope. A getter by that name finds the other net.}] 1
# a net name beats an alias, then the smaller wire: \u/x over ux with hdlname "u x"
check_eq [lsort [lmap pin [get_pins -of_objects [get_nets {u\/x}]] {get_full_name $pin}]] {i2/Y i4/A}
