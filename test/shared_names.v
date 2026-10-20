module top(input a, output y, output z);
  (* hdlname = "u x" *) wire ux;
  wire \u/x ;
  INV i1 (.A(a), .Y(ux));
  INV i2 (.A(a), .Y(\u/x ));
  INV i3 (.A(ux), .Y(y));
  INV i4 (.A(\u/x ), .Y(z));
endmodule
