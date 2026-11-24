# Tcl side of opensta_net, captures the constraint commands and applies them again after a rebuild
namespace eval ::ysta::sdc {}
namespace eval ::ysta {
	# stack, a command can run commands
	variable entered {}
	# {{mode family} cmd} that write_sdc omits, a capture adds them
	variable kept {}
	# {mode script} of the last capture, objects are tokens
	variable captured {}
	# {mode pin} of hierarchical pin disables, write_sdc omits them
	variable hier_disables {}
	# from 1 on old clock handles and stale nets exist
	variable rebuilds 0
	# clock and edge handles that a getter gave since the last rebuild, see note_handles
	variable live_handles
	# linked to C++, while set old nets get a stale name
	variable writing_sdc

	# a kept command keeps the later ones of its family, so the order stays
	variable families {
		set_input_delay in unset_input_delay in set_output_delay out unset_output_delay out
		set_max_delay path set_min_delay path unset_path_exceptions path set_load load
		set_power_activity power unset_power_activity power
		set_disable_inferred_clock_gating icg unset_disable_inferred_clock_gating icg
	}
	variable unwritten_cmds {set_clock_gating_check set_disable_inferred_clock_gating set_wire_load_model
		set_wire_load_selection_group set_pvt set_power_activity
		set_assigned_delay set_assigned_check set_assigned_transition}
	variable unwritten_options {
		set_input_delay {-network_latency_included -source_latency_included}
		set_output_delay {-network_latency_included -source_latency_included}
		set_max_delay -probe set_min_delay -probe set_load -subtract_pin_load
	}
	variable path_cmds {set_false_path set_multicycle_path set_max_delay set_min_delay set_path_margin group_path}
	variable path_end_options {-from -to -rise_from -fall_from -rise_to -fall_to}
	# wire_as per command, see WireAs in constraints.h
	variable wire_as_by_cmd {
		set_disable_timing edges set_case_analysis driven set_logic_zero driven set_logic_one driven
		set_logic_dc driven create_clock pins create_generated_clock pins set_clock_latency pins
		set_load nets set_resistance nets set_timing_derate nets set_voltage nets
	}
}

proc ::ysta::mode_exists {mode} { ::sta::is_object [::sta::find_mode $mode] }

# a command can delete a clock, so a kept command has no clock handle, only a getter by name
proc ::ysta::enter {cmd op} {
	foreach clock [regexp -all -inline {\m_[0-9a-f]+_p_Clock\M} $cmd] {
		set cmd [string map [list $clock [list "\[get_clocks [list [get_name $clock]]\]"]] $cmd]
	}
	lappend ::ysta::entered $cmd
}
proc ::ysta::record {cmd code result op} {
	set named [lindex $::ysta::entered end]
	set ::ysta::entered [lrange $::ysta::entered 0 end-1]
	if {$code != 0} return
	keep [namespace tail [lindex $cmd 0]] $cmd $named
}
proc ::ysta::keep {name cmd named} {
	set family [list [::sta::cmd_mode_name]]
	if {[dict exists $::ysta::families $name]} { lappend family [dict get $::ysta::families $name] }
	set family_is_kept [expr {[llength $family] == 2 && [lsearch -exact -index 0 $::ysta::kept $family] >= 0}]
	if {!$family_is_kept && ![unwritten $name $cmd]} return
	lappend ::ysta::kept [list $family [activity_as_density [lreplace $named 0 0 $name]]]
}
proc ::ysta::unwritten {name cmd} {
	# a rebuild drops the set_property of these objects, so we keep it
	if {$name eq "set_property"} {
		# no object gives an empty type
		return [expr {[::sta::object_type [lindex $cmd 1]] in {Pin Instance Net Clock Port}}]
	}
	if {$name in $::ysta::unwritten_cmds} { return 1 }
	if {![dict exists $::ysta::unwritten_options $name]} { return 0 }
	# OpenSTA takes a prefix of an option, same match as its parse_key_args
	set options [dict get $::ysta::unwritten_options $name]
	foreach word $cmd {
		if {[::sta::is_keyword_arg $word] && [lsearch -glob $options $word*] >= 0} { return 1 }
	}
	return 0
}
# OpenSTA divides -activity by the min clock period of now, a later run would take another period
proc ::ysta::activity_as_density {cmd} {
	set at [lsearch -exact $cmd -activity]
	if {[lindex $cmd 0] ne "set_power_activity" || $at < 0} { return $cmd }
	set period [::sta::time_sta_ui [::sta::clock_min_period [::sta::cmd_mode_name]]]
	set cmd [lreplace $cmd $at $at+1 -density [expr {[lindex $cmd $at+1] / $period}]]
	set at [lsearch -exact $cmd -clock]
	if {$at >= 0} { set cmd [lreplace $cmd $at $at+1] }
	return $cmd
}
# remove_delay_slew_annotations undoes the set_assigned_* commands, also in the kept commands and the last capture
proc ::ysta::drop_assigned {args} {
	foreach name {kept captured} {
		set ::ysta::$name [lsearch -all -inline -not -regexp -index 1 [set ::ysta::$name] {^(::)?(sta::)?set_assigned_}]
	}
}
trace add execution ::sta::remove_delay_slew_annotations leave ::ysta::drop_assigned

# OpenSTA keeps a hierarchical pin disable as wire edges of its leaf pins, so we undo it while its pins live
proc ::ysta::record_hier_disable {cmd code result op} {
	set pin [lindex $cmd 1]
	if {$code != 0 || ![$pin is_hierarchical]} return
	set item [list [::sta::cmd_mode_name] $pin]
	set disables [lsearch -all -inline -exact -not $::ysta::hier_disables $item]
	if {[namespace tail [lindex $cmd 0]] eq "disable_pin"} { lappend disables $item }
	set ::ysta::hier_disables $disables
}
foreach cmd {disable_pin unset_disable_pin} { trace add execution ::sta::$cmd leave ::ysta::record_hier_disable }
proc ::ysta::undo_hier_disables {} {
	set scene [::sta::cmd_scene]
	set cmd_mode [::sta::cmd_mode_name]
	foreach item $::ysta::hier_disables {
		lassign $item mode pin
		# a deleted mode took its disables along
		if {![mode_exists $mode]} continue
		::sta::set_cmd_mode $mode
		# the pin of an unlinked network is gone with its edges
		catch {::sta::unset_disable_pin $pin}
	}
	set ::ysta::hier_disables {}
	::sta::set_cmd_scene $scene
	::sta::set_cmd_mode $cmd_mode
}
proc ::ysta::clear {} {
	array unset ::ysta::live_handles
	foreach name {kept captured} { set ::ysta::$name {} }
}

proc ::ysta::split_commands {text} {
	set commands {}
	set script {}
	foreach line [split $text \n] {
		append script $line \n
		if {[string index $line end] eq "\\" || ![info complete $script]} continue
		if {![regexp {^\s*(#|$)} $script]} { lappend commands $script }
		set script {}
	}
	# an incomplete command stays, its evaluation reports the error
	if {[string trim $script] ne {}} { lappend commands $script }
	return $commands
}
# write_sdc writes -name raw as the first option, we brace it up to the next one
proc ::ysta::braced_name {script} {
	# joined first, else a line continuation after the name gets into the braces
	set script [string trim [string map {"\\\n" " "} $script]]
	if {![regexp -indices {^\S+ -name } $script head]} { return $script }
	set from [expr {[lindex $head 1] + 1}]
	set to [string length $script]
	if {[regexp -indices -start $from {\s+[-\[]} $script next]} { set to [lindex $next 0] }
	return [string replace $script $from $to-1 [list [string range $script $from $to-1]]]
}
proc ::ysta::written_scripts {mode} {
	close [file tempfile path]
	try {
		write_sdc -no_timestamp -mode $mode $path
		set file [open $path]
		fconfigure $file -encoding utf-8
		set scripts [lmap script [split_commands [read $file]] { braced_name $script }]
	} finally {
		catch {close $file}
		file delete $path
	}
	# write_sdc sorts by name and a new object takes the next token number, so the disables stay after its scripts
	foreach item [lsearch -all -inline -exact -index 0 $::ysta::hier_disables $mode] {
		lappend scripts "set_disable_timing \[get_pins {[get_full_name [lindex $item 1]]}\]"
	}
	return $scripts
}
# the higher the tighter
proc ::ysta::tightness {script} {
	set signs {set_min_delay 1 set_max_delay -1 set_multicycle_path -1}
	if {![regexp {\S+} $script name] || ![dict exists $signs $name]} return
	# the value is the last word before -comment
	set value [lindex [regexp -all -inline {\S+} [regsub {\s+-comment\s+(\{.*\}|\S+)$} $script {}]] end]
	if {![string is double -strict $value]} return
	expr {[dict get $signs $name] * $value}
}
# OpenSTA takes the later path exception on a shared path, so the tightest goes last
proc ::ysta::tightest_last {commands} {
	set places {}
	set by_tightness {}
	for {set at 0} {$at < [llength $commands]} {incr at} {
		set tightness [tightness [lindex $commands $at 1]]
		if {$tightness eq {}} continue
		lappend places $at
		lappend by_tightness [list $tightness [lindex $commands $at]]
	}
	foreach place $places sorted [lsort -real -index 0 $by_tightness] { lset commands $place [lindex $sorted 1] }
	return $commands
}
# an error keeps the last capture
proc ::ysta::capture {args} {
	set written {}
	foreach mode $args {
		foreach script [written_scripts $mode] { lappend written [list $mode $script] }
	}
	set written [tightest_last $written]
	# a kept unset_path_exceptions must not remove a written exception, only these run after the kept commands
	set late {}
	set exceptions {^(set_false_path|set_multicycle_path|set_path_margin|group_path)\M}
	if {[lsearch -glob -index 1 $::ysta::kept unset_path_exceptions*] >= 0} {
		set late [lsearch -all -inline -regexp -index 1 $written $exceptions]
		set written [lsearch -all -inline -not -regexp -index 1 $written $exceptions]
	}
	# a kept command runs after the written ones, so the apply keeps only it again
	# named gives out token numbers too, so it stays after written_scripts
	set kept {}
	foreach item $::ysta::kept {
		set mode [lindex $item 0 0]
		if {$mode in $args} { lappend kept [list $mode [named [lindex $item 1]]] }
	}
	set ::ysta::captured [list {*}$written {*}$kept {*}$late]
}

# get_full_name of a pin, a cell, a net or a top port gives a token in a capture
proc ::ysta::object_word {object} {
	set name [list [get_full_name $object]]
	if {[::sta::object_type $object] ne "Clock"} { return $name }
	return "\[get_clocks $name\]"
}
proc ::ysta::names {word} {
	# a clock getter of enter stays
	if {[string match {\[get_clocks *\]} $word]} { return [list $word] }
	if {[::sta::is_object $word]} { return [list [object_word $word]] }
	if {[catch {llength $word}] || [lindex $word 0] eq $word} return
	set words {}
	set has_object 0
	foreach item $word {
		set names [names $item]
		if {$names ne {}} { set has_object 1 }
		if {$names eq {}} { set names [list [list $item]] }
		lappend words {*}$names
	}
	if {$has_object} { return $words }
}
proc ::ysta::named {cmd} {
	if {[catch {llength $cmd}]} { return $cmd }
	lset cmd 0 [namespace tail [lindex $cmd 0]]
	set script {}
	foreach word $cmd {
		set names [names $word]
		switch [llength $names] {
			0 { lappend script [list $word] }
			1 { lappend script [lindex $names 0] }
			default { lappend script "\[list [join $names]\]" }
		}
	}
	join $script
}
# a getter takes the exact name with no wildcards, a name with no object is missing
proc ::ysta::exact {finder name} {
	set object [$finder $name]
	if {$object ne "NULL"} { return [list $object] }
	missing $name
}
proc ::ysta::find_port {name} { [[::sta::top_instance] cell] find_port $name }

# a getter of a captured script gives its tokens back, ::ysta::run resolves them
foreach getter {get_pins get_cells get_nets get_ports} { interp alias {} ::ysta::sdc::$getter {} ::ysta::sdc::list }
# write_sdc joins getters with list
proc ::ysta::sdc::list {args} { concat {*}$args }
# write_sdc writes a clock name raw in braces, find_clock takes the exact name, a list goes on without a lost clock
proc ::ysta::sdc::get_clocks {name} { ::ysta::exact ::sta::find_clock $name }
# write_sdc starts with current_design top, the design stays
proc ::ysta::sdc::current_design {args} {}
# the word of tokens resolves again for each pin, so a lost object is in the loss text once per pin
proc ::ysta::edges_between {froms tos} {
	set edges {}
	foreach from $froms {
		foreach to [resolve $tos] { lappend edges {*}[::get_timing_edges -from $from -to $to] }
	}
	return $edges
}
# write_sdc writes an edge disable as -from pin -to pin
proc ::ysta::sdc::get_timing_edges {from_flag froms to_flag tos} {
	set edges [::ysta::edges_between [::ysta::resolve $froms] $tos]
	# a start behind new clock logic, the edges of the starts that it clocks
	if {$edges eq {}} {
		set starts [get_fanout -from [::ysta::resolve $froms] -flat -endpoints_only -trace_arcs all]
		set edges [::ysta::edges_between $starts $tos]
	}
	if {$edges eq {}} { error "no timing edge between the pins" }
	return $edges
}
# the leaf pins of a dissolved pin or net keep its paths, clock, value and edges, another use is reported
proc ::ysta::wire_as {cmd option} {
	if {[dict exists $::ysta::wire_as_by_cmd $cmd]} { return [dict get $::ysta::wire_as_by_cmd $cmd] }
	if {[string match *through $option] || $option eq "-reference_pin"} { return pins }
	return none
}
# an option with a pin behind new logic becomes one list of words per command to run
#   -to P      -> -through P -to the path ends after P, the paths stay the same
#   -from P    -> -from the starts that P clocks
#   -rise_from -> the starts behind inverting logic get -fall_from
proc ::ysta::moved_end_words {option word} {
	lassign [moved_ends $option $word] kept moved ends starts inverted_starts
	if {$moved eq {}} return
	lappend kept {*}$starts
	set commands {}
	if {$option eq "-from"} {
		lappend kept {*}$inverted_starts
	} elseif {$inverted_starts ne {}} {
		lappend commands [list [string map {rise fall fall rise} $option] $inverted_starts]
	}
	if {$kept ne {}} { lappend commands [list $option $kept] }
	if {$ends ne {}} { lappend commands [list [string range $option 0 end-2]through $moved -to $ends] }
	return $commands
}
proc ::ysta::with_word {commands word moved} {
	if {$moved eq {}} { return [lmap words $commands { linsert $words end $word }] }
	set next {}
	foreach words $commands {
		# the moved end words take the place of the option
		foreach end_words $moved { lappend next [list {*}[lrange $words 0 end-1] {*}$end_words] }
	}
	return $next
}
proc ::ysta::has_token {word} { expr {[string first __ysta_ $word] >= 0} }
proc ::ysta::run {cmd args} {
	set cell [lindex $args end]
	if {$cmd eq "set_disable_timing" && [llength $args] > 1 && [has_token $cell]} {
		return [uplevel #0 [linsert [moved_arc $cell [lrange $args 0 end-1]] 0 ::sta::$cmd]]
	}
	set commands {{}}
	foreach word $args {
		set option [lindex $commands 0 end]
		set moved {}
		# only a lost clock leaves an empty word, resolve makes it an error
		if {$word eq {}} { resolve {} }
		if {[has_token $word]} {
			set is_path_end [expr {$cmd in $::ysta::path_cmds && $option in $::ysta::path_end_options}]
			if {$is_path_end} { set moved [moved_end_words $option $word] }
			set word [resolve $word [wire_as $cmd $option]]
			# a pin that flatten dissolved is several pins of one clock, the command takes one
			if {$option eq "-reference_pin"} { set word [lrange $word 0 0] }
		}
		set commands [with_word $commands $word $moved]
	}
	# sdc_file_line of OpenSTA reads the text of each command on the stack as a list, so no {*} here
	foreach words $commands { uplevel #0 [linsert $words 0 ::sta::$cmd] }
}
proc ::ysta::run_scripts {space scripts} {
	set lost 0
	foreach item $scripts {
		lassign $item mode text
		if {![mode_exists $mode]} continue
		::sta::set_cmd_mode $mode
		set failed [catch {namespace eval $space $text} error]
		# one line for the log text, a user word of a kept script can have a backslash-newline
		set log_text [string trim [regsub -all {\s+} [string map {"\\\n" " "} $text] { }]]
		incr lost [loss $failed $error $log_text]
	}
	return $lost
}
proc ::ysta::apply {} {
	set scene [::sta::cmd_scene]
	set cmd_mode [::sta::cmd_mode_name]
	# sta_warn and sta_error add the script file and line to messages
	set script [info script]
	info script {}
	set ::ysta::kept {}
	set lost [run_scripts ::ysta::sdc $::ysta::captured]
	# the scene sets its own mode
	::sta::set_cmd_scene $scene
	::sta::set_cmd_mode $cmd_mode
	info script $script
	# a new clock can get the handle word of an old one, so the handles that the scripts got do not stay
	array unset ::ysta::live_handles
	return $lost
}

# OpenSTA crashes on an empty first argument
proc ::ysta::skip_empty {cmd first args} {
	if {[llength $first]} { tailcall $cmd $first {*}$args }
}
foreach cmd {set_clock_sense_cmd find_fanin_pins find_fanin_insts find_fanout_pins find_fanout_insts} {
	if {[catch {rename ::sta::$cmd ::sta::ysta_$cmd} err]} { error "cannot wrap $cmd: $err" }
	interp alias {} ::sta::$cmd {} ::ysta::skip_empty ::sta::ysta_$cmd
}
foreach {why cmds} {
	{network edits are not supported, edit the design in yosys.}
	{read_verilog_cmd clear_network make_instance_cmd delete_instance_cmd replace_cell_cmd make_net_cmd
	 delete_net_cmd connect_pin_cmd disconnect_pin_cmd make_port_pin_cmd}
	{it would clear the constraints, the next opensta_net call rebuilds the network.} network_changed
	{the STA instance belongs to opensta_net.} delete_all_memory
} { foreach cmd $cmds { proc ::sta::$cmd {args} [list error "Error: 2929 [regsub {_cmd$} $cmd {}]: $why"] } }

# Sdc::clear keeps the resistance and voltage of nets of an earlier network, we drop their stale name lines
proc ::ysta::drop_stale_lines {path gzip} {
	set in [open $path rb]
	if {$gzip} { zlib push gunzip $in }
	set lines [split [read $in] \n]
	close $in
	set kept [lsearch -all -inline -not -glob $lines *__ysta_stale*]
	if {[llength $kept] == [llength $lines]} return
	set out [open $path wb]
	if {$gzip} { zlib push gzip $out }
	puts -nonewline $out [join $kept \n]
	close $out
}
if {[catch {rename ::sta::write_sdc_cmd ::sta::ysta_write_sdc_cmd} err]} { error "cannot wrap write_sdc_cmd: $err" }
proc ::sta::write_sdc_cmd {filename mode leaf compatible digits gzip no_timestamp} {
	set ::ysta::writing_sdc 1
	try {
		::sta::ysta_write_sdc_cmd $filename $mode $leaf $compatible $digits $gzip $no_timestamp
	} finally {
		set ::ysta::writing_sdc 0
	}
	set path [file normalize $filename]
	# an unknown mode writes no file, a device is not read back
	if {$::ysta::rebuilds == 0 || ![::ysta::mode_exists $mode] || ![file isfile $path]} return
	::ysta::drop_stale_lines $path $gzip
}

# a rebuild frees the clocks and the timing edges, a new object can get the memory of an old one
proc ::ysta::note_handles {cmd code result op} {
	foreach handle [regexp -all -inline {\m_[0-9a-f]+_p_(?:Clock|Edge)\M} $result] {
		set ::ysta::live_handles($handle) 1
	}
}
foreach cmd {get_clocks find_clock get_property get_timing_edges} {
	trace add execution ::sta::$cmd leave ::ysta::note_handles
}
# we refuse a handle that no getter gave since the rebuild
proc ::ysta::check_handles {cmd op} {
	foreach {handle type} [regexp -all -inline {\m_[0-9a-f]+_p_(Clock|Edge)\M} $cmd] {
		if {[info exists ::ysta::live_handles($handle)]} continue
		set object [dict get {Clock clock Edge {timing edge}} $type]
		error "Error: 2931 a $object handle of an earlier network names no object, get the $object again."
	}
}
# traces and no wrapper procs, namespace import binds the command, so a rename leaves the import unwrapped
foreach cmd [array names ::sta::cmd_args] {
	if {![regexp {^(create_|set_|unset_|delete_|group_path)} $cmd]} continue
	if {$cmd in {delete_from_list delete_instance delete_net set_cmd_units set_mode set_scene}} continue
	if {[info commands ::sta::$cmd] eq {}} continue
	trace add execution ::sta::$cmd enter ::ysta::enter
	# the last trace runs first, a refused handle then leaves nothing in entered
	trace add execution ::sta::$cmd enter ::ysta::check_handles
	trace add execution ::sta::$cmd leave ::ysta::record
	interp alias {} ::ysta::sdc::$cmd {} ::ysta::run $cmd
}
