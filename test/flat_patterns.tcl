# after flatten a pattern matches flat cell names, u1.r for u1/r
source test/lib/check.tcl
link_top test/data/flat.v
yosys flatten
proc names {objs} { lsort [lmap obj $objs {get_full_name $obj}] }
check_eq [names [get_cells u1.*]] {u1/i0 u1/r u1/u2/j u1/u2/k}
check_eq [names [get_pins u1.u2.*/Y]] {u1/u2/j/Y u1/u2/k/Y}
yosys design -reset
link_top [temp_file {module sub(input a, output y); INV r(.A(a), .Y(y)); endmodule
module top(input a, output y, z, w); sub u1(.a(a), .y(y)); sub u10(.a(a), .y(z)); INV u1A(.A(a), .Y(w)); endmodule}]
yosys flatten
# the matches keep the order of the cell names
check_eq [lmap c [get_cells *] {get_full_name $c}] {u10/r u1A u1/r}
