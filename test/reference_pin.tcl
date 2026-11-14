# a -reference_pin that becomes several pins of one clock keeps one
source test/lib/check.tcl
link_track
create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1.5 -clock clk -reference_pin [get_pins u1/clk] [get_ports {in[3]}]
yosys flatten
check_eq [lost_cmds [log_of opensta_net -sync]] {}
set line [lsearch -inline [split [written_sdc] \n] {set_input_delay *}]
# one pin, a list of pins has a space
check_eq [regexp {^set_input_delay 1.5000 -clock \[get_clocks \{clk\}\] -add_delay -reference_pin \[get_pins \{\S*u1\.\S*/CK\}\] \[get_ports \{in\[3\]\}\]$} $line] 1
