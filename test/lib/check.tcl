# test helpers, each test in test/ sources this file first

set nangate deps/OpenSTA/test/nangate45/Nangate45_typ.lib
set sky130 deps/OpenSTA/test/sky130hd/sky130hd_tt.lib

# fail when got is not want, name the first line that differs
# the error ends the test, test/run.sh prints it
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

# count the lines matching the pattern, only * is a wildcard
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

# moved_pins.v as gates with _reg flop names, linked on nangate
proc link_moved_pins {} {
	yosys read_verilog test/data/moved_pins.v
	yosys hierarchy -top top
	yosys proc
	yosys opt_dff
	yosys techmap
	yosys rename -wire -suffix _reg {t:$_DFF*}
	yosys opensta_net -liberty test/data/moved_pins.lib
	yosys opensta_net -liberty $::nangate -link
}

# track.v mapped to nangate in a new design, linked
proc link_track {} {
	yosys design -reset
	yosys read_verilog test/data/track.v
	yosys hierarchy -top top
	yosys proc
	yosys opt
	yosys techmap
	yosys opt
	yosys dfflibmap -liberty $::nangate
	yosys abc -liberty $::nangate
	yosys opensta_net -liberty $::nangate -link
}

# the sorted full names of the objects
proc names {objs} { lsort [lmap obj $objs {get_full_name $obj}] }

# the yosys log of one yosys command
# on a command error, the error message is that log
proc log_of {args} {
	set path [temp_file {}]
	set failed [catch {yosys tee -o $path {*}$args}]
	set log [read_file $path]
	file delete $path
	return -code $failed $log
}

# the yosys log of a tcl script
# it holds the syncs of its STA queries
# yosys tcl ends the epoch first, as each pass does
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

# the lost counts of the sync after a yosys command with a capture that fails
proc fail_capture {args} {
	rename write_sdc ysta_test_write_sdc
	proc ::write_sdc {args} { error "no write" }
	yosys {*}$args
	rename write_sdc {}
	rename ysta_test_write_sdc write_sdc
	sync_losses [log_of opensta_net -sync]
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

# sta binary output on a script, with OpenSTA's verilog reader
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
