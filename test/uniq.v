// Two instances of one module: after uniquify, two copies.
module sub(input a, output y);
  BUF_X1 b (.A(a), .Z(y));
endmodule
module top(input i, output o1, output o2);
  sub u1 (.a(i), .y(o1));
  sub u2 (.a(i), .y(o2));
endmodule
