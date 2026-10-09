read_liberty test/simple.lib
read_verilog test/barrier_out.v
link_design top
source test/barrier_body.tcl
