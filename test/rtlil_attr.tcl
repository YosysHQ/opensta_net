# rtlil::set_attr changes the design without a Monitor event or a pass run:
# the pass start hooks end the epoch, as for a `yosys` command.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/rtlil_attr.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
create_clock -period 10 -name clk [get_ports clk]
proc net_of {pin} { get_full_name [get_nets -of_objects [get_pins $pin]] }
puts "NET [net_of u_inv1/Y]"
rtlil::set_attr -true top zz keep
puts "LINKED [sta::network_is_linked]"
yosys opensta_net -sync
puts "NET [net_of u_inv1/Y]"
yosys opensta_net -autosync
rtlil::set_attr -false top zz keep
puts "LINKED [sta::network_is_linked]"
puts "NET [net_of u_inv1/Y]"
