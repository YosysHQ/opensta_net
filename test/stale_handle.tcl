# a handle of an earlier network is an error, not freed memory
source test/lib/check.tcl
link_top test/data/simple.v
set pin [get_pins u_inv2/Y]
yosys opensta_net -link
check_eq [error_of {get_full_name $pin}] {Error: 2931 a handle of an earlier network names no object, get the object again.}
check_eq [get_full_name [get_pins u_inv2/Y]] u_inv2/Y
