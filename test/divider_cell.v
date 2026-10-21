// a top cell whose name has the divider, and the cell it names in sub
module sub(input a, output y);
  INV x (.A(a), .Y(y));
endmodule
module top(input a, output y, output z);
  wire n;
  INV \u/x (.A(a), .Y(n));
  INV i2 (.A(n), .Y(z));
  sub u (.a(a), .y(y));
endmodule
