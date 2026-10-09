create_clock -period 10 -name clk [get_ports clk]

set_max_delay 3 -through [get_nets no_such_net]
no_such_command
set_max_delay 4 -through [get_nets n1b]
