module S(input a, output y); INV c(.A(a), .Y(y)); endmodule
module P(input a, output y); (* keep_hierarchy *) S s(.a(a), .y(y)); endmodule
module top(input a, output y1, output y2, output y3); P pa(.a(a), .y(y1)); P pb(.a(a), .y(y2)); S t(.a(a), .y(y3)); endmodule
