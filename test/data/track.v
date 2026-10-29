module sub(input clk, input [3:0] a, input b, output [3:0] y);
  reg [3:0] r;
  wire [3:0] t = a ^ {4{b}};
  always @(posedge clk) r <= t;
  assign y = r & a;
endmodule
module div(input clk, input e, output o);
  reg d, s;
  always @(posedge clk) d <= ~d;
  always @(posedge d) s <= e;
  assign o = s;
endmodule
module top(input clk, input [3:0] in, input sel, output [3:0] out, output o2, output o3, output o4);
  wire [3:0] m;
  wire p = in[0] & sel;
  sub u1(.clk(clk), .a(in), .b(p), .y(m));
  div u2(.clk(clk), .e(in[1]), .o(o4));
  reg q;
  always @(posedge clk) q <= ^m;
  assign out = m | {4{q}};
  assign o2 = q;
  assign o3 = p ^ in[2];
endmodule
