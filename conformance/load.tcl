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
	yosys tee -q design -reset
	foreach lib $::ysta_libs { yosys tee -q read_liberty -lib $lib }
	yosys tee -q read_verilog $::ysta_verilog
	yosys tee -q hierarchy -top $top
	yosys tee -q uniquify
	yosys tee -q opensta_net -link
}
