# opt_clean on a clean mapped design changes no net
source test/lib/check.tcl
yosys read_verilog deps/yosys/tests/functional/picorv32.v
yosys read_liberty -lib $sky130
yosys synth -top picorv32
yosys dfflibmap -liberty $sky130
yosys abc -liberty $sky130
yosys opt_clean -purge
check_eq [count_lines [log_of opensta_net -liberty $sky130 -link] {opensta_net: built network: *}] 1
# it rewrites connections, the view stays valid, no rebuild
yosys opt_clean
check_eq [sync_losses [log_of opensta_net -sync]] {}
yosys opt_clean -purge
check_eq [sync_losses [log_of opensta_net -sync]] {}
