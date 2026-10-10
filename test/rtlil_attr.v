// zz is an alias of n1: `keep` on zz renames the net.
module top(input clk, input in, output out);
  wire d0, q0, n1, zz;
  assign zz = n1;
  INV u_inv0 (.A(in), .Y(d0));
  DFF u_dff0 (.CLK(clk), .D(d0), .Q(q0));
  INV u_inv1 (.A(q0), .Y(n1));
  INV u_inv2 (.A(zz), .Y(out));
endmodule
