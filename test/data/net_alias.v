module src(input a, output async_data_o); INV r(.A(a), .Y(async_data_o)); endmodule
module dst(input async_data_i, output y); INV l(.A(async_data_i), .Y(y)); endmodule
module cdc(input a, output y); wire async_data; src i_src(.a(a), .async_data_o(async_data)); dst i_dst(.async_data_i(async_data), .y(y)); endmodule
module top(input a, output y); cdc i_cdc(.a(a), .y(y)); endmodule
