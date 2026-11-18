# after flatten the hierarchical name also finds a cell, a pin or a net, write_sdc keeps the yosys name
source test/lib/check.tcl
link_top test/data/flat.v
yosys opensta_net -unprotect
yosys flatten
check_eq [names [get_cells u1/r]] u1.r
check_eq [names [get_pins u1/r/Q]] u1.r/Q
check_eq [names [get_nets u1/mid]] u1.mid
check_eq [names [get_cells u1/u2/*]] {u1.u2.j u1.u2.k}
check_eq [names [get_pins u1/*/D]] u1.r/D
check_eq [names [get_nets u1/u2/*]] u1.u2.n
set_false_path -to [get_pins u1/r/D]
check_eq [count_lines [written_sdc] {set_false_path -to [get_pins {u1.r/D}]}] 1