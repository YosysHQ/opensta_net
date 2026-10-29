// Object order (object_order.tcl): statement order is not name order on purpose.
// m[0] and n have several loads, u2 has all bits of A on n.
module sub(input clk, input d);
  DFF ra (.CLK(clk), .D(d), .Q());
endmodule
module top(input clk, input [3:0] in, output [3:0] out, output y);
  wire [3:0] m;
  wire n;
  BUF4 u1 (.A(in), .Y(m));
  INV i3 (.A(m[0]), .Y(n));
  BUF4 u0 (.A(m), .Y(out));
  DFF r2 (.CLK(clk), .D(n), .Q());
  sub s1 (.clk(clk), .d(m[2]));
  INV i1 (.A(n), .Y(y));
  BUF4 u2 (.A({n, n, n, n}), .Y());
  DFF r1 (.CLK(clk), .D(m[1]), .Q());
endmodule
