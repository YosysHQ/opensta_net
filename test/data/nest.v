module leaf(input a, output y); wire n; INV j(.A(a), .Y(n)); INV k(.A(n), .Y(y)); endmodule
module mid(input a, output y); wire m; leaf b(.a(a), .y(m)); leaf c(.a(m), .y(y)); endmodule
module top(input clk, input a, input b, output y1, output y2, output y3);
  wire t;
  mid u1(.a(a), .y(y1));
  mid u2(.a(b), .y(t));
  INV g(.A(t), .Y(y2));
  leaf u3(.a(t), .y(y3));
endmodule
