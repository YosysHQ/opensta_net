# an opensta_net query ends no epoch
source test/lib/check.tcl
link_top test/data/bus.v
yosys opensta_net -eval {set_input_delay 1 [get_ports {in[1]}]}
yosys opensta_net -eval {report_checks}
check_eq [rtlil::has_attr top in netview_ref] 0
yosys stat
check_eq [rtlil::has_attr top in netview_ref] 1
yosys select -assert-count 1 {t:$barrier}
