# flatten removes hierarchical pins: a -through output keeps the paths that
# leave, a case value goes on the loads inside
source test/lib/check.tcl
yosys read_verilog test/data/hier_thru.v
yosys hierarchy -top top
yosys proc
yosys techmap
yosys rename -wire -suffix _reg {t:$_*DFF*}
yosys opensta_net -liberty test/data/moved_pins.lib -link
create_clock -name clk -period 10 [get_ports clk]
set_max_delay 5 -through [get_pins u/q]
set_case_analysis 1 [get_pins u/a]
yosys flatten
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [[find_timing_paths -from [get_cells u/r_reg] -to [get_cells u/r_reg]] is_path_delay] 0
