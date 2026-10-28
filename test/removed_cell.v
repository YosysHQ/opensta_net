// opt_expr -mux_undef makes the mux a wire, abc -g cmos2 maps the and to two cells
module top(input clk, input a, input b, input s, output reg q, output y);
  wire n = s ? ~a : 1'bx;
  always @(posedge clk) q <= n;
  assign y = a & ~b;
endmodule
