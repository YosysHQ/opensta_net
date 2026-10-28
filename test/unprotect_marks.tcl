# -unprotect removes every mark but the anchors of the epoch end before write_verilog
yosys read_verilog test/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/moved_pins.lib -link
set_false_path -through [get_cells u]
yosys opensta_net -unprotect
yosys write_verilog test/unprotect_marks_out.v
puts "MARKS [regexp -all -inline {netview_[a-z_]*|keep[a-z_]*} [read [open test/unprotect_marks_out.v]]]"
