# -unprotect removes every mark but the anchors of the epoch end before
# write_verilog
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib -link
set_false_path -through [get_cells u]
yosys opensta_net -unprotect
yosys write_verilog test/out/unprotect_marks.v
check_eq [regexp -all -inline {netview_[a-z_]*|keep[a-z_]*} [read_file test/out/unprotect_marks.v]] netview_ref
