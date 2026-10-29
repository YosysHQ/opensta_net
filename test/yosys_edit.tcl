# a yosys edit after the link changes the timing that OpenSTA reports
source test/lib/check.tcl
link_top test/data/simple.v
yosys opensta_net -sdc test/data/simple.sdc
check_eq [format %.2f [worst_slack -max]] 8.60
# u_inv2 skips u_inv1
yosys connect -port u_inv2 A q0
check_eq [format %.2f [worst_slack -max]] 8.70
