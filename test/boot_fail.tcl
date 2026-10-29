# a boot that cannot wrap an OpenSTA command ends the run with an error
source test/lib/check.tcl
lassign [yosys_child {
	namespace eval ::sta {}
	proc ::sta::ysta_find_fanout_insts {args} {}
	yosys opensta_net -eval {puts BOOTED}
	puts ALIVE
}] code out
check_eq $code 1
check_eq [count_lines $out {ERROR: OpenSTA: cannot wrap find_fanout_insts: can't rename to "::sta::ysta_find_fanout_insts": command already exists}] 1
check_eq [count_lines $out BOOTED] 0
check_eq [count_lines $out ALIVE] 0
