module big(input clk, input resetn);
  picorv32_1 u1 (.clk(clk), .resetn(resetn));
  picorv32_2 u2 (.clk(clk), .resetn(resetn));
  picorv32_3 u3 (.clk(clk), .resetn(resetn));
  picorv32_4 u4 (.clk(clk), .resetn(resetn));
  picorv32_5 u5 (.clk(clk), .resetn(resetn));
  picorv32_6 u6 (.clk(clk), .resetn(resetn));
  picorv32_7 u7 (.clk(clk), .resetn(resetn));
  picorv32_8 u8 (.clk(clk), .resetn(resetn));
endmodule
