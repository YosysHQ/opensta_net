// the register drives a load inside and the output pin
module sub(input clk, input a, output q);
  reg r;
  always @(posedge clk) r <= a & ~r;
  assign q = r;
endmodule
module top(input clk, input a, output y);
  sub u(.clk(clk), .a(a), .q(y));
endmodule
