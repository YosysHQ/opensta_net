module top(input a, output y, output z);
  wire zero_;
  INV i1 (.A(a), .Y(zero_));
  INV i2 (.A(1'b0), .Y(y));
  INV i3 (.A(zero_), .Y(z));
endmodule
