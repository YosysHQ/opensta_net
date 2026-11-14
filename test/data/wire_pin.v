module top(input clk, input a, input s, output reg q, output reg q2);
  wire gclk = s ? clk : 1'bx;
  always @(posedge gclk) q <= a;
  always @(posedge clk) q2 <= q;
endmodule
