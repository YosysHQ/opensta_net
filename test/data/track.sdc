create_clock -name clk -period 10 [get_ports clk]
set_input_delay 1.1 -clock clk [get_ports {in[*]}]
set_output_delay 1.2 -clock clk [get_ports {out[*]}]
create_generated_clock -name gclk -source [get_ports clk] -divide_by 2 [get_pins -of_objects [get_nets {u2/d}] -filter "direction==output"]
# a clock on a hierarchical pin, which flatten removes
create_clock -name hclk -period 20 [get_pins u1/clk]
# nets: top, bus bit, inside a scope
set_max_delay 2.1 -through [get_nets p]
set_max_delay 2.2 -through [get_nets {m[2]}]
set_max_delay 2.3 -through [get_nets {u1/t[1]}]
set_max_delay 2.5 -through [get_nets {u1/r[0]}]
# a part of the flat net p, which flatten merges into it
set_false_path -through [get_nets {u1/b}]
# hierarchical instance and pin
set_max_delay 2.4 -through [get_cells u1]
set_max_delay 2.6 -through [get_pins u1/b]
# register pins
set_max_delay 2.7 -through [all_registers -output_pins]
set_min_delay 0.3 -from [all_registers -clock_pins]
# combinational pins and cells, 2.8 is on the paths of 2.3
set_max_delay 2.8 -through [get_pins -of_objects [get_nets {u1/t[1]}] -filter "direction==output"]
set_max_delay 2.9 -through [get_pins -of_objects [get_nets {u1/t[2]}] -filter "direction==output"]
set_disable_timing [get_cells -of_objects [get_nets p] -filter "is_hierarchical==false"]
