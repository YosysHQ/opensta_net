# -eval and -unprotect in one call, the SDC of -eval has the command of -eval, -unprotect then ends the network
# flatten moves a clock on a hierarchical pin to its driver
source test/lib/check.tcl
link_top test/data/flat.v
create_clock -name hc -period 10 [get_pins u1/u2/y]
set_max_delay 3 -to [get_pins u1/r/D]
yosys flatten
set path [temp_file {}]
set log [log_of opensta_net -eval [subst -nocommands {
	set_false_path -through [get_cells u1/u2/k]
	write_sdc -no_timestamp $path
}] -unprotect]
check_eq [lost_cmds $log] {}
set sdc [regsub -all {\s*\\\n\s*} [read_file $path] " "]
file delete $path
check_eq [count_lines $sdc {create_clock -name hc -period 10.0000 [get_pins {u1.u2.k/Y}]}] 1
check_eq [count_lines $sdc {set_false_path -through [get_cells {u1.u2.k}]}] 1
yosys select -assert-none t:\$barrier
check_eq [sta::network_is_linked] 0
