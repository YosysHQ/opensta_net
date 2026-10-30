# a rebuild makes new design cells: a user property on an old one shows on none, a liberty cell keeps its value
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
define_property -object_type cell -type string tag
set_property [[get_cells u] cell] tag SUB
set lib_cell [[lindex [get_cells -hierarchical -filter "is_hierarchical==false" *] 0] cell]
set_property $lib_cell tag LIB
yosys add -wire dummy top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [get_property [[get_cells u] cell] tag] {}
check_eq [get_property [[sta::top_instance] cell] tag] {}
check_eq [get_property $lib_cell tag] LIB
