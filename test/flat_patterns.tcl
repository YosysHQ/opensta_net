# after flatten a cell has its yosys name, u1.r
source test/lib/check.tcl
link_top test/data/flat.v
yosys flatten
check_eq [names [get_cells u1.*]] {u1.i0 u1.r u1.u2.j u1.u2.k}
check_eq [names [get_pins u1.u2.*/Y]] {u1.u2.j/Y u1.u2.k/Y}
yosys design -reset
link_top [temp_file {module sub(input a, output y); INV r(.A(a), .Y(y)); endmodule
module top(input a, output y, z, w); sub u1(.a(a), .y(y)); sub u10(.a(a), .y(z)); INV u1A(.A(a), .Y(w)); endmodule}]
yosys flatten
# the matches are in name order
check_eq [lmap c [get_cells *] {get_full_name $c}] {u1.r u10.r u1A}
