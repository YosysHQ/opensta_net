// Run through `prep -barriers`: every driven public wire gets a $barrier.
module top(input clk, input in, input [3:0] bi, output out, output [3:0] bo);
  wire d0, q0, n1;
  wire [3:0] nb;
  INV u_inv0 (.A(in), .Y(d0));
  DFF u_dff0 (.CLK(clk), .D(d0), .Q(q0));
  INV u_inv1 (.A(q0), .Y(n1));
  INV u_inv2 (.A(n1), .Y(out));
  BUF4 u_buf0 (.A(bi), .Y(nb));
  BUF4 u_buf1 (.A(nb), .Y(bo));
endmodule
