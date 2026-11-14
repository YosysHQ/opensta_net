# the Tcl side of opensta_net, guards, journal and apply helpers
# a captured script runs in ::ysta::sdc (see ::ysta::run), a replayed one in ::ysta::byname (see ::ysta::exact)
namespace eval ::ysta::sdc {}
namespace eval ::ysta::byname { namespace path ::ysta::sdc }
namespace eval ::ysta {
	# {mode cmd} of the succeeded constraint commands since the last capture, see name_journal
	variable journal {}
	# {{mode family} cmd} of the commands write_sdc does not write, a capture adds them, the apply records them again
	variable kept {}
	# a kept command keeps the later ones of its family, they run in the order given
	variable families {
		set_input_delay in unset_input_delay in set_output_delay out unset_output_delay out
		set_max_delay path set_min_delay path unset_path_exceptions path set_load load
		set_power_activity power unset_power_activity power
		set_disable_inferred_clock_gating icg unset_disable_inferred_clock_gating icg
	}
	# {mode script} of the journal commands that no capture has, the apply runs them again by name
	variable replay {}
	# {mode script} of the last capture
	variable captured {}
	# how many times the constraints were applied again, see sync_first
	variable rebuilds 0
	# {mode pin} of each hierarchical pin disable
	variable hier_disables {}
	# the commands whose -from and -to take sets of path ends
	variable path_cmds {set_false_path set_multicycle_path set_max_delay set_min_delay set_path_margin group_path}
	variable unwritten_cmds {set_clock_gating_check set_disable_inferred_clock_gating set_wire_load_model
		set_wire_load_selection_group set_pvt set_power_activity
		set_assigned_delay set_assigned_check set_assigned_transition}
	# by command, OpenSTA takes a unique prefix of an option
	variable unwritten_options {
		set_input_delay {-network_latency_included -source_latency_included}
		set_output_delay {-network_latency_included -source_latency_included}
		set_max_delay -probe set_min_delay -probe set_load -subtract_pin_load
	}
}

# skip an empty first argument, OpenSTA crashes on it
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
	{it would clear the constraints, opensta_net -sync rebuilds the network.} network_changed
	{the STA instance belongs to opensta_net.} delete_all_memory
} { foreach cmd $cmds { proc ::sta::$cmd {args} [list error "Error: 2929 [regsub {_cmd$} $cmd {}]: $why"] } }
foreach cmd {rtlil::set_attr rtlil::set_param} { trace add execution $cmd enter ::ysta::before_edit }
foreach cmd {yosys rtlil::set_attr rtlil::set_param} { trace add execution $cmd leave ::ysta::after_edit }

proc ::ysta::record {cmd code result op} {
	if {$code != 0} return
	lappend ::ysta::journal [list [::sta::cmd_mode_name] $cmd]
	if {[catch {llength $cmd}]} return
	set name [namespace tail [lindex $cmd 0]]
	set family [list [::sta::cmd_mode_name]]
	if {[dict exists $::ysta::families $name]} { lappend family [dict get $::ysta::families $name] }
	set in_family [expr {[llength $family] == 2 && [lsearch -exact -index 0 $::ysta::kept $family] >= 0}]
	if {!$in_family && ![::ysta::unwritten $name $cmd]} return
	lappend ::ysta::kept [list $family [::ysta::activity_as_density [lreplace $cmd 0 0 $name]]]
}
# set_power_activity -activity as -density over the min clock period of now
proc ::ysta::activity_as_density {cmd} {
	set at [lsearch -exact $cmd -activity]
	if {[lindex $cmd 0] ne "set_power_activity" || $at < 0} { return $cmd }
	set period [::sta::time_sta_ui [::sta::clock_min_period [::sta::cmd_mode_name]]]
	set cmd [lreplace $cmd $at $at+1 -density [expr {[lindex $cmd $at+1] / $period}]]
	set at [lsearch -exact $cmd -clock]
	if {$at >= 0} { set cmd [lreplace $cmd $at $at+1] }
	return $cmd
}
# remove_delay_slew_annotations undoes the set_assigned_* commands, also in the last capture and the journal
proc ::ysta::drop_assigned {args} {
	foreach name {kept captured journal replay} {
		set ::ysta::$name [lsearch -all -inline -not -regexp -index 1 [set ::ysta::$name] {^(::)?(sta::)?set_assigned_}]
	}
}
trace add execution ::sta::remove_delay_slew_annotations leave ::ysta::drop_assigned
# a query syncs first so the next capture has the command, a rebuild frees the clocks, we refuse an old clock handle
proc ::ysta::sync_first {cmd op} {
	set rebuilds $::ysta::rebuilds
	::sta::network_is_linked
	if {$rebuilds != $::ysta::rebuilds && [string match *_p_Clock* $cmd]} {
		error "Error: 2931 a clock handle of an earlier network names no object, get the clock again."
	}
}
foreach cmd {get_clocks all_clocks} { trace add execution ::sta::$cmd enter ::ysta::sync_first }
proc ::ysta::clear {} {
	::ysta::undo_hier_disables
	foreach name {journal kept replay captured} { set ::ysta::$name {} }
}
# OpenSTA keeps a hierarchical pin disable as wire edges of its leaf pins, write_sdc does not write it
# the capture writes it, undo_hier_disables removes it while its pins live
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
		if {![::sta::is_object [::sta::find_mode $mode]]} continue
		::sta::set_cmd_mode $mode
		# the pin of an unlinked network is gone with its edges
		catch {::sta::unset_disable_pin $pin}
	}
	set ::ysta::hier_disables {}
	::sta::set_cmd_scene $scene
	::sta::set_cmd_mode $cmd_mode
}
# Sdc::clear keeps the resistance and voltage of nets of an earlier network, we drop their stale name lines
if {[catch {rename ::sta::write_sdc_cmd ::sta::ysta_write_sdc_cmd} err]} { error "cannot wrap write_sdc_cmd: $err" }
proc ::sta::write_sdc_cmd {filename mode leaf compatible digits gzip no_timestamp} {
	set ::ysta::writing_sdc 1
	try {
		::sta::ysta_write_sdc_cmd $filename $mode $leaf $compatible $digits $gzip $no_timestamp
	} finally {
		set ::ysta::writing_sdc 0
	}
	# no stale net before the first rebuild, an unknown mode writes no file, a device is not read back
	set path [file normalize $filename]
	if {$::ysta::rebuilds == 0 || ![::sta::is_object [::sta::find_mode $mode]] || ![file isfile $path]} return
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
proc ::ysta::unwritten {name cmd} {
	# a rebuild drops the set_property of these objects, so we keep it
	if {$name eq "set_property"} {
		if {[catch {::sta::object_type [lindex $cmd 1]} type]} { return 0 }
		return [expr {$type in {Pin Instance Net Clock Port}}]
	}
	if {$name in $::ysta::unwritten_cmds} { return 1 }
	if {![dict exists $::ysta::unwritten_options $name]} { return 0 }
	foreach word $cmd {
		if {![string match -?* $word]} continue
		foreach option [dict get $::ysta::unwritten_options $name] {
			if {[string equal -length [string length $word] $word $option]} { return 1 }
		}
	}
	return 0
}
# the names of the objects in a word as script words, nested lists flat, none when the word has no object
# an object is the getter of its type or of other, else its name, a token in token mode for a pin, a cell or a net
proc ::ysta::names {word getters} {
	if {[::sta::is_object $word]} {
		set name [list [get_full_name $word]]
		set type [::sta::object_type $word]
		if {![dict exists $getters $type]} { set type other }
		if {[dict exists $getters $type]} { set name "\[[dict get $getters $type] $name\]" }
		return [list $name]
	}
	if {[catch {llength $word}] || [lindex $word 0] eq $word} { return {} }
	set found [lmap item $word { ::ysta::names $item $getters }]
	if {[concat {*}$found] eq {}} { return {} }
	# a plain name beside an object stays
	concat {*}[lmap item $word names $found { if {$names eq {}} { list [list $item] } else { set names } }]
}
# the command as script text with each object as its name, a word with no object stays as it is
proc ::ysta::named {cmd {getters {Clock get_clocks}}} {
	if {[catch {llength $cmd}]} { return $cmd }
	lset cmd 0 [namespace tail [lindex $cmd 0]]
	join [lmap word $cmd {
		set names [::ysta::names $word $getters]
		if {[llength $names] == 1} {
			lindex $names 0
		} elseif {$names eq {}} {
			list $word
		} else {
			string cat "\[list " [join $names] "\]"
		}
	}]
}
# before the link while a handle still gives its name, a journal command becomes a script that gets objects by name
proc ::ysta::name_journal {} {
	set getters {
		Clock get_clocks Pin get_pins Instance get_cells Net get_nets Port get_ports
		LibertyCell get_lib_cells LibertyLibrary get_libs other ::ysta::missing
	}
	foreach item $::ysta::journal {
		lappend ::ysta::replay [list [lindex $item 0] [::ysta::named [lindex $item 1] $getters]]
	}
	set ::ysta::journal {}
}
# a getter of a replayed script takes the exact name with no wildcards, a name with no object is missing
proc ::ysta::exact {finder name} {
	set object [$finder $name]
	if {$object ne "NULL"} { return [list $object] }
	::ysta::missing $name
}
proc ::ysta::find_port {name} { [[::sta::top_instance] cell] find_port $name }
foreach {getter finder} {
	get_pins ::sta::find_pin get_cells ::sta::find_instance get_nets ::sta::find_net get_ports ::ysta::find_port
} { interp alias {} ::ysta::byname::$getter {} ::ysta::exact $finder }
# record each constraint command in the journal, a captured script runs it by ::ysta::run
foreach cmd [array names ::sta::cmd_args] {
	if {![regexp {^(create_|set_|unset_|delete_|group_path)} $cmd]} continue
	if {$cmd in {delete_from_list delete_instance delete_net set_cmd_units set_mode set_scene}} continue
	if {[info commands ::sta::$cmd] eq {}} continue
	trace add execution ::sta::$cmd enter ::ysta::sync_first
	trace add execution ::sta::$cmd leave ::ysta::record
	interp alias {} ::ysta::sdc::$cmd {} ::ysta::run $cmd
}

# write_sdc of each mode becomes the captured commands, an error keeps the last capture and the journal replays by name
proc ::ysta::capture {args} {
	set commands {}
	foreach mode $args {
		foreach script [::ysta::written_scripts $mode] { lappend commands [list $mode $script] }
	}
	# a kept command runs after the written ones, so the apply keeps only it again
	set commands [::ysta::tightest_last $commands]
	# a kept unset_path_exceptions must not remove a written exception, only these run after the kept commands
	set late {}
	set exceptions {^(set_false_path|set_multicycle_path|set_path_margin|group_path)\M}
	if {[lsearch -glob -index 1 $::ysta::kept unset_path_exceptions*] >= 0} {
		set late [lsearch -all -inline -regexp -index 1 $commands $exceptions]
		set commands [lsearch -all -inline -not -regexp -index 1 $commands $exceptions]
	}
	foreach item $::ysta::kept {
		if {[lindex $item 0 0] in $args} { lappend commands [list [lindex $item 0 0] [::ysta::named [lindex $item 1]]] }
	}
	lappend commands {*}$late
	set ::ysta::captured $commands
	set ::ysta::journal {}
}
# the scripts that write_sdc writes for the mode, then its hierarchical pin disables
proc ::ysta::written_scripts {mode} {
	close [file tempfile path]
	try {
		write_sdc -no_timestamp -mode $mode $path
		set file [open $path]
		fconfigure $file -encoding utf-8
		set scripts [lmap script [::ysta::commands [read $file]] { ::ysta::braced_name $script }]
	} finally {
		catch {close $file}
		file delete $path
	}
	foreach item [lsearch -all -inline -exact -index 0 $::ysta::hier_disables $mode] {
		lappend scripts "set_disable_timing \[get_pins {[get_full_name [lindex $item 1]]}\]"
	}
	return $scripts
}
proc ::ysta::commands {text} {
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
# line continuations and whitespace runs collapsed, only for the log text and the order
proc ::ysta::one_line {script} { string trim [regsub -all {\s+} [string map {"\\\n" " "} $script] { }] }
# joins only the line continuations, braces the -name that write_sdc writes raw as first option up to the next one
proc ::ysta::braced_name {script} {
	set script [string trim [string map {"\\\n" " "} $script]]
	if {![regexp -indices {^\S+ -name } $script head]} { return $script }
	set from [expr {[lindex $head 1] + 1}]
	set to [string length $script]
	if {[regexp -indices -start $from {\s+[-\[]} $script next]} { set to [lindex $next 0] }
	return [string replace $script $from $to-1 [list [string range $script $from $to-1]]]
}
# tightest path exception last, OpenSTA takes the later one on a shared path, the value is the last word before -comment
proc ::ysta::tightest_last {commands} {
	set signs {set_min_delay 1 set_max_delay -1 set_multicycle_path -1}
	set places {}
	set order {}
	set at -1
	foreach command $commands {
		incr at
		set script [lindex $command 1]
		regexp {\S+} $script name
		if {![dict exists $signs $name]} continue
		set value [lindex [split [regsub {\s+-comment\s+(\{.*\}|\S+)$} [::ysta::one_line $script] {}]] end]
		if {![string is double -strict $value]} continue
		lappend places $at
		lappend order [list [expr {[dict get $signs $name] * $value}] $command]
	}
	foreach place $places sorted [lsort -real -index 0 $order] { lset commands $place [lindex $sorted 1] }
	return $commands
}

# a getter gives its tokens back, a constraint command is ::ysta::run
foreach getter {get_pins get_cells get_nets get_ports} { interp alias {} ::ysta::sdc::$getter {} ::ysta::sdc::list }
# write_sdc writes a clock name raw in braces, find_clock takes the exact name, a list goes on without a lost clock
proc ::ysta::sdc::get_clocks {name} { ::ysta::exact ::sta::find_clock $name }
# write_sdc joins getters with list
proc ::ysta::sdc::list {args} { concat {*}$args }
# write_sdc writes an edge disable as -from pin -to pin
proc ::ysta::sdc::get_timing_edges {from_flag froms to_flag tos} {
	set edges {}
	foreach from [::ysta::resolve $froms] {
		foreach to [::ysta::resolve $tos] { lappend edges {*}[::get_timing_edges -from $from -to $to] }
	}
	# a start behind new clock logic, the edges of the starts that it clocks
	if {$edges eq {}} {
		foreach from [get_fanout -from [::ysta::resolve $froms] -flat -endpoints_only -trace_arcs all] {
			foreach to [::ysta::resolve $tos] { lappend edges {*}[::get_timing_edges -from $from -to $to] }
		}
	}
	if {$edges eq {}} { error "no timing edge between the pins" }
	return $edges
}
# write_sdc starts with current_design top, the design stays
proc ::ysta::sdc::current_design {args} {}
# one list of words per command to run for an option with a pin behind new logic, nothing when no pin moved
#   the objects that stayed keep the option
#   -to P      -> -through P -to the path ends after P, the paths stay the same
#   -from P    -> -from the starts that P clocks
#   -rise_from -> the starts behind inverting logic get -fall_from
proc ::ysta::moved_end_words {option word} {
	lassign [::ysta::moved_ends $option $word] kept moved ends starts inverted_starts
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
# runs the captured then the replayed commands on the new network, returns how many are lost or changed
proc ::ysta::apply {} {
	incr ::ysta::rebuilds
	set lost 0
	set scene [::sta::cmd_scene]
	set cmd_mode [::sta::cmd_mode_name]
	# sta_warn and sta_error add the script file and line to messages
	set script [info script]
	info script {}
	set ::ysta::kept {}
	foreach {space commands} [list ::ysta::sdc $::ysta::captured ::ysta::byname $::ysta::replay] {
		# the apply journaled the captured commands again, a replayed one stays in the journal for the next capture
		set ::ysta::journal {}
		foreach command $commands {
			lassign $command mode text
			if {![::sta::is_object [::sta::find_mode $mode]]} continue
			::sta::set_cmd_mode $mode
			set failed [catch {namespace eval $space $text} error]
			incr lost [::ysta::loss $failed $error [::ysta::one_line $text]]
		}
	}
	set ::ysta::replay {}
	# the scene sets its own mode
	::sta::set_cmd_scene $scene
	::sta::set_cmd_mode $cmd_mode
	info script $script
	return $lost
}
# a cell that is a wire now is the loads behind it, only where they have just its paths
# the leaf pins of a dissolved pin or net keep its paths, clock, value and edges, another use is reported
proc ::ysta::wire_as {cmd option} {
	if {$cmd eq "set_disable_timing"} { return edges }
	if {[string match set_logic_* $cmd] || $cmd eq "set_case_analysis"} { return driven }
	if {$cmd in {create_clock create_generated_clock set_clock_latency}} { return pins }
	if {[string match *through $option] || $option eq "-reference_pin"} { return pins }
	return none
}
# one constraint command on the found objects, a word of tokens with no object is an error and the command is lost
# a word with path ends behind new logic runs once for each list of words of ::ysta::moved_end_words
proc ::ysta::run {cmd args} {
	set cell [lindex $args end]
	if {$cmd eq "set_disable_timing" && [llength $args] > 1 && [string first __ysta_ $cell] >= 0} {
		return [uplevel #0 [linsert [::ysta::moved_arc $cell [lrange $args 0 end-1]] 0 ::sta::$cmd]]
	}
	set commands {{}}
	foreach word $args {
		set option [lindex $commands 0 end]
		set moved {}
		# only lost clocks
		if {$word eq {}} { ::ysta::resolve {} }
		if {[string first __ysta_ $word] >= 0} {
			if {$cmd in $::ysta::path_cmds && [regexp {^-(rise_|fall_)?(from|to)$} $option]} {
				set moved [::ysta::moved_end_words $option $word]
			}
			set word [::ysta::resolve $word [::ysta::wire_as $cmd $option]]
			# a pin that flatten dissolved is several pins of one clock, the command takes one
			if {$option eq "-reference_pin"} { set word [lrange $word 0 0] }
		}
		set next {}
		foreach words $commands {
			if {$moved eq {}} { lappend next [linsert $words end $word] }
			foreach end_words $moved { lappend next [concat [lrange $words 0 end-1] $end_words] }
		}
		set commands $next
	}
	foreach words $commands { uplevel #0 [linsert $words 0 ::sta::$cmd] }
}
