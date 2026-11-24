# source before an OpenSTA test
set ::ysta_libs {}
proc read_liberty {args} {
	lappend ::ysta_libs [lindex $args end]
	sta::read_liberty {*}$args
}
proc read_verilog {file} {
	set ::ysta_verilog $file
}
proc link_design {top} {
	::ysta::load_design $::ysta_libs $::ysta_verilog $top
}
