# a netlist that another yosys wrote after its epoch end holds the anchors and the barriers of its own design
source test/lib/check.tcl
set path test/out/foreign_anchors.il
lassign [yosys_child "
	source test/lib/check.tcl
	link_top test/data/simple.v
	set_false_path -through \[get_cells u_inv0\]
	yosys opt_clean
	yosys write_rtlil $path
"] code out
check_eq $code 0
link_top test/data/top_a.v a
set_false_path -through [get_cells u_inv1]
yosys design -reset
yosys read_rtlil $path
yosys hierarchy -top top
check_eq [sync_losses [log_of opensta_net -sync]] 1
check_eq [regexp -all {u_inv} [written_sdc]] 0
