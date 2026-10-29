# Tcl side of opensta_net: guards, the constraint journal, apply helpers
namespace eval ::ysta {
	variable journal {}
	variable kept {}
	variable kept_families {}
	# not written by write_sdc
	variable unwritten_cmds {set_clock_gating_check set_disable_inferred_clock_gating set_wire_load_model
		set_wire_load_selection_group set_pvt set_power_activity set_property}
	variable unwritten_options {-network_latency_included -source_latency_included -probe -subtract_pin_load}
	# a kept command keeps the later ones of its family
	variable families {
		set_input_delay in unset_input_delay in
		set_output_delay out unset_output_delay out
		set_max_delay path set_min_delay path unset_path_exceptions path
		set_load load
		set_disable_timing disable unset_disable_timing disable
		set_power_activity power unset_power_activity power
		set_disable_inferred_clock_gating icg unset_disable_inferred_clock_gating icg
	}
	# set_property on these is kept
	variable property_types {Pin Instance Net Clock Port}
}

# Guards

# skip an empty first argument, opensta crashes on it
proc ::ysta::skip_empty {cmd first args} {
	if {[llength $first]} { tailcall $cmd $first {*}$args }
}
foreach cmd {set_clock_sense_cmd find_fanin_pins find_fanin_insts find_fanout_pins find_fanout_insts} {
	if {[catch {rename ::sta::$cmd ::sta::ysta_$cmd} err]} { error "cannot wrap $cmd: $err" }
	interp alias {} ::sta::$cmd {} ::ysta::skip_empty ::sta::ysta_$cmd
}
foreach {cmd why} {
	read_verilog_cmd {read_verilog: network edits are not supported, edit the design in yosys.}
	clear_network {clear_network: network edits are not supported, edit the design in yosys.}
	network_changed {network_changed: it would clear the constraints, opensta_net -sync rebuilds the network.}
	delete_all_memory {delete_all_memory: the STA instance belongs to opensta_net.}
} { proc ::sta::$cmd {args} [list error "Error: 2929 $why"] }
# recheck the view after an edit
foreach cmd {yosys rtlil::set_attr rtlil::set_param} { trace add execution $cmd leave ::ysta::after_yosys }
# end the epoch before a tcl edit
foreach cmd {rtlil::set_attr rtlil::set_param} { trace add execution $cmd enter ::ysta::before_edit }

# ---- Journal

# journal each command, keep the unwritten ones
proc ::ysta::record {cmd code result op} {
	variable journal
	variable kept
	variable kept_families
	variable unwritten_cmds
	variable families
	variable property_types
	if {$code != 0} return
	set mode [::sta::cmd_mode_name]
	lappend journal [list $mode $cmd]
	if {[catch {llength $cmd}]} return
	set name [namespace tail [lindex $cmd 0]]
	if {$name ni $unwritten_cmds && ![dict exists $families $name]} return
	set family {}
	if {[dict exists $families $name]} { set family [list $mode [dict get $families $name]] }
	if {$name eq "set_property" && ([catch {::sta::object_type [lindex $cmd 1]} type] || $type ni $property_types)} return
	if {![::ysta::unwritten $name $cmd] && ![dict exists $kept_families $family]} return
	lappend kept [list $mode [::ysta::activity_as_density $cmd]]
	if {$family ne {}} { dict set kept_families $family 1 }
}
proc ::ysta::clear {} {
	set ::ysta::journal {}
	set ::ysta::kept {}
	set ::ysta::kept_families {}
}
proc ::ysta::clear_journal {} { set ::ysta::journal {} }
proc ::ysta::kept_count {} { return [llength $::ysta::kept] }
# {texts untexted} of kept or journal
proc ::ysta::texts {which} {
	set texts [dict create]
	set untexted 0
	foreach item [set ::ysta::$which] {
		lassign $item mode cmd
		if {[catch {::ysta::sdc_text $cmd} text]} { incr untexted; continue }
		dict append texts $mode "$text\n"
	}
	return [list $texts $untexted]
}
# 1 if write_sdc does not write it
proc ::ysta::unwritten {name cmd} {
	variable unwritten_cmds
	variable unwritten_options
	if {$name in $unwritten_cmds} { return 1 }
	foreach option $unwritten_options { if {$option in $cmd} { return 1 } }
	if {$name ne "set_disable_timing"} { return 0 }
	foreach word [lrange $cmd 1 end] {
		foreach obj [::ysta::objects $word] {
			if {[::sta::object_type $obj] eq "Pin" && [$obj is_hierarchical]} { return 1 }
		}
	}
	return 0
}
# -activity as -density
proc ::ysta::activity_as_density {cmd} {
	if {[namespace tail [lindex $cmd 0]] ne "set_power_activity"} { return $cmd }
	set at [lsearch -exact $cmd -activity]
	if {$at < 0} { return $cmd }
	set period [::sta::time_sta_ui [::sta::clock_min_period [::sta::cmd_mode_name]]]
	set cmd [lreplace $cmd $at $at+1 -density [expr {[lindex $cmd $at+1] / $period}]]
	set at [lsearch -exact $cmd -clock]
	if {$at >= 0} { set cmd [lreplace $cmd $at $at+1] }
	return $cmd
}
# the sta objects in a word
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
# the command as sdc text
proc ::ysta::sdc_text {cmd} {
	set words [list [namespace tail [lindex $cmd 0]]]
	foreach word [lrange $cmd 1 end] {
		set objs [::ysta::objects $word]
		if {$objs eq {}} { lappend words [list $word]; continue }
		set getters [lmap obj $objs { ::ysta::getter $obj }]
		if {[llength $getters] == 1} { lappend words [lindex $getters 0]; continue }
		lappend words "\[list [join $getters { }]\]"
	}
	return [join $words " "]
}
# a getter by name
proc ::ysta::getter {obj} {
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
	error "no getter for $type"
}
# record the constraint commands
foreach cmd [array names ::sta::cmd_args] {
	if {![regexp {^(create_|set_|unset_|delete_|group_path)} $cmd]} continue
	if {$cmd in {delete_from_list delete_instance delete_net set_cmd_units set_mode set_scene}} continue
	if {[info commands ::sta::$cmd] ne {}} { trace add execution ::sta::$cmd leave ::ysta::record }
}

# Apply

# the edges between the pins, else the wire edges into froms
proc ::ysta::edges {froms tos} {
	set edges {}
	foreach from $froms {
		set from_edges {}
		foreach to $tos { lappend from_edges {*}[get_timing_edges -from $from -to $to] }
		if {$from_edges eq {}} { return [::ysta::wire_edges $froms] }
		lappend edges {*}$from_edges
	}
	return $edges
}
# rise and fall swap behind an inverter
proc ::ysta::start_edge {flag froms} {
	foreach from $froms {
		foreach edge [get_timing_edges -from $from] {
			if {[::ysta::inverts $edge]} { return [string map {rise fall fall rise} $flag] }
		}
	}
	return $flag
}
# 1 if each arc inverts
proc ::ysta::inverts {edge} {
	set arcs [$edge timing_arcs]
	foreach arc $arcs {
		if {[$arc from_edge_name] eq [$arc to_edge_name]} { return 0 }
	}
	return [expr {$arcs ne {}}]
}
# the wire edges into the pins
proc ::ysta::wire_edges {pins} {
	set edges {}
	foreach pin $pins {
		foreach edge [get_timing_edges -to $pin] {
			if {[$edge role] eq "wire"} { lappend edges $edge }
		}
	}
	if {$edges eq {}} { error "no wire edge into the pins" }
	return $edges
}
# 1 if each edge into founds inverts and comes from an exact pin
proc ::ysta::behind_inverter {founds exacts} {
	set nets {}
	foreach exact $exacts { lappend nets [get_full_name [get_nets -of_objects $exact]] }
	foreach found $founds {
		set edges [get_timing_edges -to $found]
		if {$edges eq {}} { return 0 }
		foreach edge $edges {
			set net [get_full_name [get_nets -of_objects [$edge from_pin]]]
			if {![::ysta::inverts $edge] || $net ni $nets} { return 0 }
		}
	}
	return 1
}
