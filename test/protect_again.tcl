# -protect puts the barriers again after -unprotect, the constraints stay
source test/lib/check.tcl
link_top test/data/sync.v
create_clock -period 10 -name clk [get_ports clk]
set_false_path -through [get_pins u_a/Y]
yosys opensta_net -unprotect
yosys opt_clean
yosys select -assert-none t:\$barrier
yosys opensta_net -protect
yosys opt_clean
yosys select -assert-min 1 t:\$barrier
check_eq [count_lines [written_sdc] {set_false_path -through [get_pins {u_a/Y}]}] 1
