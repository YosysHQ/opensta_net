read_liberty test/simple.lib
read_verilog test/bus1.v
link_design top
source test/bus1_body.tcl
