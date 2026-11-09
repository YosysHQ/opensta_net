# -link resets the unbuildable warning, a failed -link stops even a pending rebuild
source test/lib/check.tcl
link_top test/data/simple.v
yosys setattr -mod -unset top top
check_eq [count_lines [log_of opensta_net -sync] {*cannot rebuild the STA network: *}] 1
yosys setattr -mod -set top 1 top
yosys opensta_net -link
yosys setattr -mod -unset top top
check_eq [count_lines [log_of opensta_net -sync] {*cannot rebuild the STA network: *}] 1
yosys setattr -mod -set top 1 top
yosys chtype -set {$_NOT_} top/u_inv1
check_eq [count_lines [log_of opensta_net -sync] {*cannot rebuild the STA network (*}] 1
error_of {yosys opensta_net -link}
yosys chtype -set INV top/u_inv1
check_eq [sta::network_is_linked] 0
