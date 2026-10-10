// Net-name test (lost_rename.tcl): one net, two public names after a connect.
module top(input a, output y);
  wire w1;
  INV u1 (.A(a), .Y(w1));
  INV u2 (.A(w1), .Y(y));
endmodule
