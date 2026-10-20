# get_fanout and get_fanin of an empty list are empty
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/simple.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
puts "FANOUT [llength [get_fanout -from {}]] [llength [get_fanout -from {} -only_cells]]"
puts "FANIN [llength [get_fanin -to {}]] [llength [get_fanin -to {} -only_cells]]"
