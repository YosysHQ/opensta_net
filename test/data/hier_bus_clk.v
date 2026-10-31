module sub(input [1:0] clk, input d, output q);
  DFF r (.CLK(clk[1]), .D(d), .Q(q));
endmodule
module top(input c0, input c1, input in, output out);
  sub u (.clk({c1, c0}), .d(in), .q(out));
endmodule
