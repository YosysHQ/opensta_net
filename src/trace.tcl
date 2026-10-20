# skip calls with an empty first argument, opensta crashes on them
foreach cmd {set_clock_sense_cmd find_fanin_pins find_fanin_insts find_fanout_pins find_fanout_insts} {
	if {[catch {rename ::sta::$cmd ::sta::ysta_$cmd} err]} { error "cannot wrap $cmd: $err" }
	proc ::sta::$cmd {first args} [string map [list CMD $cmd] {if {[llength $first]} { tailcall ::sta::ysta_CMD $first {*}$args }}]
}
# refuse network edits and sta resets
foreach {cmd why} {
	read_verilog_cmd {read_verilog: network edits are not supported, edit the design in yosys.}
	clear_network {clear_network: network edits are not supported, edit the design in yosys.}
	network_changed {network_changed: it would clear the constraints, opensta_net -sync rebuilds the network.}
	delete_all_memory {delete_all_memory: the STA instance belongs to opensta_net.}
} { proc ::sta::$cmd {args} [list error "Error: 2929 $why"] }
# recheck the view after an edit
foreach cmd {yosys rtlil::set_attr rtlil::set_param} { trace add execution $cmd leave ysta::after_yosys }
# end the epoch before a tcl edit
foreach cmd {rtlil::set_attr rtlil::set_param} { trace add execution $cmd enter ysta::before_edit }
# record getter results for -keep_hierarchy
foreach cmd {get_cells get_pins get_nets get_fanin get_fanout all_registers} { trace add execution $cmd leave ysta::record_result }
