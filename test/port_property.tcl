# a rebuild makes new top ports, also when the top module gets a new port: a user property stays on its own port only
source test/lib/check.tcl
link_top test/data/simple.v
define_property -object_type port -type string tag
set_property [get_ports clk] tag CLK
yosys add -input new_in 1 top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [llength [get_ports *]] 4
foreach port [get_ports *] {
	set want {}
	if {[get_name $port] eq "clk"} { set want CLK }
	check_eq [get_property $port tag] $want
}
