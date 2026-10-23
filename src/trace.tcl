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
# sdc commands since the last epoch end, as {mode command}
namespace eval ::ysta {
	variable journal {}
}
proc ::ysta::clear {} {
	set ::ysta::journal {}
}
proc ::ysta::objects {word} {
	set objs {}
	if {[catch {llength $word}]} { return {} }
	foreach item $word {
		if {[llength $item] > 1} {
			lappend objs {*}[::ysta::objects $item]
		} elseif {[::sta::is_object $item]} {
			lappend objs $item
		}
	}
	return $objs
}
proc ::ysta::sdc_text {cmd} {
	set words [list [namespace tail [lindex $cmd 0]]]
	foreach word [lrange $cmd 1 end] {
		set objs [::ysta::objects $word]
		if {$objs eq {}} { lappend words [list $word]; continue }
		set refs {}
		foreach obj $objs { lappend refs [::ysta::ref $obj] }
		if {[llength $objs] == 1} { lappend words [lindex $refs 0]; continue }
		lappend words "\[list [join $refs { }]\]"
	}
	return [join $words " "]
}
# object names are tokens at an epoch end
proc ::ysta::ref {obj} {
	set type [::sta::object_type $obj]
	switch $type {
		Pin {
			if {[$obj is_top_level_port]} { return "\[get_ports {[get_name [$obj port]]}\]" }
			return "\[get_pins {[get_full_name $obj]}\]"
		}
		Instance { return "\[get_cells {[get_full_name $obj]}\]" }
		Net { return "\[get_nets {[get_full_name $obj]}\]" }
		Port { return "\[get_ports {[get_name $obj]}\]" }
		Clock { return "\[get_clocks {[get_name $obj]}\]" }
		LibertyCell { return "\[get_lib_cells {[get_full_name $obj]}\]" }
		LibertyPort { return "\[get_lib_pins {[get_full_name $obj]}\]" }
	}
	error "no ref for $type"
}
proc ::ysta::note_journal {cmd code result op} {
	if {$code == 0} { lappend ::ysta::journal [list [::sta::cmd_mode_name] $cmd] }
}
proc ::ysta::texts {name} {
	set ::ysta::untexted 0
	set texts [dict create]
	foreach item [set $name] {
		if {[catch {::ysta::sdc_text [lindex $item 1]} text]} {
			incr ::ysta::untexted
			continue
		}
		dict append texts [lindex $item 0] "$text\n"
	}
	return $texts
}
foreach cmd [array names ::sta::cmd_args] {
	set network {delete_from_list delete_instance delete_net set_cmd_units set_mode set_scene}
	if {![regexp {^(create_|set_|unset_|delete_|group_path)} $cmd] || $cmd in $network} continue
	if {[info commands ::sta::$cmd] ne {}} { trace add execution ::sta::$cmd leave ::ysta::note_journal }
}
