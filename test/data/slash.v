module leaf(input a, output y); wire n; INV j(.A(a), .Y(n)); INV k(.A(n), .Y(y)); endmodule
module mid(input a, output y); wire m; leaf b(.a(a), .y(m)); leaf c(.a(m), .y(y)); endmodule
module top(input a, input b, input c, input d, output y1, output y2, output y3, output y4);
  leaf u(.a(a), .y(y1));
  leaf \u/v (.a(b), .y(y2));
  leaf u10(.a(c), .y(y3));
  mid \u/q (.a(d), .y(y4));
endmodule
