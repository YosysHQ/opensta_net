// dfflibmap puts an inverter before the clock of rn and a mux before D of re
module top(input clk, input a, input en, output y1, output y2);
  reg rn, re, s;
  always @(negedge clk) rn <= a;
  always @(posedge clk) if (en) re <= a;
  always @(posedge clk) s <= rn;
  assign y1 = s;
  assign y2 = re;
endmodule
