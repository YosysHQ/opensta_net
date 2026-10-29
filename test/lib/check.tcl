# helpers for the tests in test/, each test sources this file first.
# a failed check ends the test with an error, test/run.sh prints it.

set nangate deps/OpenSTA/test/nangate45/Nangate45_typ.lib
set sky130 deps/OpenSTA/test/sky130hd/sky130hd_tt.lib

# fail when got is not want, name the first line that differs
proc check_eq {got want} {
	if {$got eq $want} return
	set gots [split $got \n]
	set wants [split $want \n]
	set n [expr {max([llength $gots], [llength $wants])}]
	for {set i 0} {$i < $n && [lindex $gots $i] eq [lindex $wants $i]} {incr i} {}
	puts "got:\n$got\nwant:\n$want"
	set got_line [string range [lindex $gots $i] 0 199]
	set want_line [string range [lindex $wants $i] 0 199]
	error "check_eq failed at line [expr {$i + 1}]: got {$got_line} want {$want_line}"
}

# the error message of a script that must fail
proc error_of {script} {
	if {![catch {uplevel 1 $script} msg]} { error "no error from: $script" }
	return $msg
}

# the number of lines of the text that match the pattern, only * is a wildcard
proc count_lines {text pattern} {
	set pattern [string map {[ \\[ ] \\] ? \\? \\ \\\\} $pattern]
	set count 0
	foreach line [split $text \n] { incr count [string match $pattern $line] }
	return $count
}

proc read_file {path} {
	set file [open $path]
	set text [read $file]
	close $file
	return $text
}

# the path of a new temp file with the text
proc temp_file {text} {
	set file [file tempfile path]
	puts -nonewline $file $text
	close $file
	return $path
}

# read the liberty and the verilog into yosys, link the top
proc link_top {verilog {top top} {lib test/data/simple.lib}} {
	yosys read_liberty -lib $lib
	yosys read_verilog $verilog
	yosys hierarchy -top $top
	yosys opensta_net -liberty $lib -link
}

# the yosys log of one yosys command. On a command error, the error message is that log
proc log_of {args} {
	set path [temp_file {}]
	set failed [catch {yosys tee -o $path {*}$args}]
	set log [read_file $path]
	file delete $path
	return -code $failed $log
}

# the yosys log of a tcl script, with the syncs of its STA queries and of -autosync.
# the yosys tcl command that runs it ends the epoch first, as each pass does
proc log_of_tcl {script} {
	set path [temp_file [list uplevel #0 $script]]
	set failed [catch {log_of tcl $path} log]
	file delete $path
	return -code $failed $log
}

# the lost count of each sync in a log
proc sync_losses {log} {
	set losses {}
	foreach {line lost} [regexp -all -inline -line {synced after design change, (\d+) constraint} $log] {
		lappend losses $lost
	}
	return $losses
}

# the commands that the syncs in a log report lost or changed
proc lost_cmds {log} {
	set cmds {}
	foreach {line cmd} [regexp -all -inline -line {sync: `(.*)` lost or changed} $log] { lappend cmds $cmd }
	return $cmds
}

# the STA output of a script, puts included
proc sta_out {script} {
	sta::redirect_string_begin
	set failed [catch {uplevel 1 $script} msg]
	set out [sta::redirect_string_end]
	if {$failed} { error $msg }
	return $out
}

# the output of the sta binary, with OpenSTA's own verilog reader, on a script
proc reference {script} {
	set path [temp_file $script]
	set failed [catch {exec -keepnewline $::env(STA) -no_init -no_splash -exit $path 2>@1} out]
	file delete $path
	return -code $failed $out
}

# the exit code and the output of a script in a separate yosys
proc yosys_child {script} {
	set path [temp_file $script]
	set code [catch {exec $::env(YOSYS) -m $::env(PLUGIN) -c $path 2>@1} out options]
	file delete $path
	if {$code} { set code [lindex [dict get $options -errorcode] 2] }
	return [list $code $out]
}

# write_sdc, one command per line
proc written_sdc {} {
	set path [temp_file {}]
	write_sdc -no_timestamp $path
	set sdc [read_file $path]
	file delete $path
	return [regsub -all {\s*\\\n\s*} $sdc " "]
}

# the commands that write_sdc does not write, the sync applies them again.
# the only read of plugin internals (::ysta::texts), grouped by mode
proc kept_cmds {} {
	lassign [::ysta::texts kept] texts untexted
	check_eq $untexted 0
	return [string trimright [join [dict values $texts] {}] \n]
}
