# constraints on hierarchical pins survive flatten
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
# a -through output keeps the paths that leave
# a case value goes on the loads inside
yosys flatten
check_eq [lost_cmds [log_of opensta_net -sync]] {}
check_eq [[find_timing_paths -from [get_cells u.r_reg] -to [get_cells u.r_reg]] is_path_delay] 0
