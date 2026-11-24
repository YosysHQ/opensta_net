# -source runs in the OpenSTA interpreter, it has no yosys command
# the capture of a failed call has the commands before the error
source test/lib/check.tcl
link_top test/data/simple.v
set log [error_of {log_of opensta_net -source [temp_file {
	set_false_path -through [get_pins u_inv1/Y]
	error "sourced [info commands yosys] [info commands rtlil::*]."
}]}]
check_eq [count_lines $log {* sourced  .}] 1
yosys add -wire w top
check_eq [sync_losses [log_of opensta_net]] 0
check_eq [count_lines [written_sdc] {set_false_path -through [get_pins {u_inv1/Y}]}] 1
