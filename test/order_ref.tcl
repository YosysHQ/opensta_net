read_liberty test/simple.lib
read_verilog test/order.v
link_design top
source test/order_body.tcl
