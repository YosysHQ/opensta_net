# rtlil::set_attr changes the design without a Monitor event or a pass: it
# ends the epoch as a yosys command does (zz is an alias of n1, keep on zz
# names the net zz)
source test/lib/check.tcl
link_top test/data/rtlil_attr.v
create_clock -period 10 -name clk [get_ports clk]
proc net_of {pin} { get_full_name [get_nets -of_objects [get_pins $pin]] }
check_eq [net_of u_inv1/Y] n1
rtlil::set_attr -true top zz keep
check_eq [sta::network_is_linked] 1
yosys opensta_net -sync
check_eq [net_of u_inv1/Y] zz
yosys opensta_net -autosync
rtlil::set_attr -false top zz keep
check_eq [sta::network_is_linked] 1
check_eq [net_of u_inv1/Y] n1
