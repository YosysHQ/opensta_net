read_liberty deps/OpenSTA/test/nangate45/Nangate45_typ.lib
read_verilog $::env(UNIQ_V)
link_design top
source test/uniq_body.tcl
