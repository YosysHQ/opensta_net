// ports declared [0:0] are buses of one bit
module bb(input [0:0] a, output y);
endmodule
module sub(input [0:0] s, output y);
  bb u(.a(s), .y(y));
endmodule
module top(input [0:0] in, output y);
  sub h(.s(in), .y(y));
endmodule
