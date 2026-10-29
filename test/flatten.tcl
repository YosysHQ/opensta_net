# constraints on hierarchical objects survive flatten: the sync applies them
# all, the hierarchical names find the same objects, write_sdc writes the same
# sdc and the timing stays
source test/lib/check.tcl
link_top test/data/flat.v
yosys opensta_net -sdc test/data/flat.sdc
proc names {objs} { lsort [lmap obj $objs {get_full_name $obj}] }
proc objects {} {
	return [join [list \
		[format %.2f [worst_slack -max]] \
		[names [get_cells {u1/r u1/u2/j}]] \
		[names [get_pins {u1/r/Q u1/u2/k/Y}]] \
		[names [get_nets {u1/mid u1/u2/n}]] \
		[names [get_cells u1/u2/*]] \
		[names [get_pins u1/*/D]]] \n]
}
set objects "1.65
u1/r u1/u2/j
u1/r/Q u1/u2/k/Y
u1/mid u1/u2/n
u1/u2/j u1/u2/k
u1/r/D"
check_eq [objects] $objects
check_eq [names [get_nets -of_objects [get_pins u1/r/Q]]] u1/y
set sdc [written_sdc]
yosys flatten
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [objects] $objects
check_eq [names [get_nets -of_objects [get_pins u1/r/Q]]] y
check_eq [written_sdc] $sdc
yosys select -assert-count 2 {t:$scopeinfo}
