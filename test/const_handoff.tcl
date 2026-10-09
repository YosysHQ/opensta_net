read_liberty deps/OpenSTA/test/nangate45/Nangate45_typ.lib
read_verilog test/const_out.v
link_design top
source test/const_body.tcl
