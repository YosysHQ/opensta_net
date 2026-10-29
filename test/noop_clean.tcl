# opt_clean on a clean mapped design rewrites connections behind the Monitor
# but changes no net: the view stays valid, no rebuild
source test/lib/check.tcl
yosys read_verilog deps/yosys/tests/functional/picorv32.v
yosys read_liberty -lib $sky130
yosys synth -top picorv32
yosys dfflibmap -liberty $sky130
yosys abc -liberty $sky130
yosys opt_clean -purge
check_eq [count_lines [log_of opensta_net -liberty $sky130 -link] {opensta_net: built network: *}] 1
yosys opt_clean
check_eq [sync_losses [log_of opensta_net -sync]] {}
yosys opt_clean -purge
check_eq [sync_losses [log_of opensta_net -sync]] {}
