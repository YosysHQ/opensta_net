// Two identical inverter pairs. u_a/u_b are referenced by the SDC, u_e/u_f
// are not: `opt_merge -share_all` folds duplicates, `opt_clean -purge` then
// drops orphaned net names.
module top(input clk, input in, output out, output out2, output out3, output out4);
  wire d0, q0, n1, n1b, e1, f1;
  INV u_inv0 (.A(in), .Y(d0));
  DFF u_dff0 (.CLK(clk), .D(d0), .Q(q0));
  INV u_a (.A(q0), .Y(n1));
  INV u_b (.A(q0), .Y(n1b));
  INV u_c (.A(n1), .Y(out));
  INV u_d (.A(n1b), .Y(out2));
  INV u_e (.A(d0), .Y(e1));
  INV u_f (.A(d0), .Y(f1));
  INV u_g (.A(e1), .Y(out3));
  INV u_h (.A(f1), .Y(out4));
endmodule
