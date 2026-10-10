proc names {objs} {
	set result {}
	foreach obj $objs { lappend result [get_full_name $obj] }
	return [lsort $result]
}
puts "SLACK [format %.2f [worst_slack -max]]"
puts "CELL [names [get_cells {u1/r u1/u2/j}]]"
puts "PIN [names [get_pins {u1/r/Q u1/u2/k/Y}]]"
puts "NET [names [get_nets {u1/mid u1/u2/n}]]"
puts "WILD [names [get_cells u1/u2/*]] [names [get_pins u1/*/D]]"
puts "QNET [names [get_nets -of_objects [get_pins u1/r/Q]]]"
# test/flat_1.sdc before flatten, test/flat_2.sdc after
if {![info exists ::flat_round]} { set ::flat_round 0 }
incr ::flat_round
write_sdc -no_timestamp test/flat_$::flat_round.sdc
