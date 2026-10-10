# A rebuild drops parasitics: say so.
yosys read_liberty -lib test/simple.lib
yosys read_verilog test/sync.v
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
sta::set_pi_model u_a/Y 0.01 1 0.01
yosys add -wire dummy top
yosys opensta_net -sync
