# A rebuild that re-resolves a constraint by a name that is now an alias
# loses nothing: losses are commands that fail or warn, not text changes.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/names.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
set_load 0.01 [get_nets w1]
yosys add -wire w0 1 top
yosys setattr -set keep 1 w:w0
yosys connect -nomap -set w0 w1 top
yosys opensta_net -sync
puts "LOAD [get_full_name [get_nets w1]]"
