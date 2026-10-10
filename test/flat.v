module inner(input a, output y); wire n; INV j(.A(a), .Y(n)); INV k(.A(n), .Y(y)); endmodule
module sub(input clk, input a, output y); wire mid, m2; INV i0(.A(a), .Y(mid)); inner u2(.a(mid), .y(m2)); DFF r(.CLK(clk), .D(m2), .Q(y)); endmodule
module top(input clk, input a, output y); sub u1(.clk(clk), .a(a), .y(y)); endmodule
