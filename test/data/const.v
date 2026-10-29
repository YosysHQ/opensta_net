// A constant pin: OpenSTA's reader names the constant net zero_.
module top(input clk, input a, output y);
  wire q, n;
  DFF_X1 r1 (.D(a), .CK(clk), .Q(q));
  AND2_X1 g (.A1(q), .A2(1'b0), .ZN(n));
  DFF_X1 r2 (.D(n), .CK(clk));
  BUF_X1 b (.A(q), .Z(y));
endmodule
