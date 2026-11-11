# barriers do not block rename -wire
source test/lib/check.tcl
yosys read_verilog test/data/moved_pins.v
yosys hierarchy -top top
yosys proc
yosys opt_dff
yosys techmap
yosys opensta_net -liberty test/data/moved_pins.lib
yosys opensta_net -liberty $nangate -link
create_clock -name clk -period 10 [get_ports clk]
set_max_delay 5 -from [all_registers -output_pins]
yosys opt_clean
yosys select -assert-min 1 t:\$barrier
yosys rename -wire -suffix _reg {t:$_DFF*}
yosys select -assert-count 3 {t:$_DFF*} n:*_reg %i
# -unprotect leaves no cut wire that autoname named
yosys autoname
yosys opensta_net -unprotect
yosys opt_clean -purge
yosys select -assert-none w:*netview_anchor*
yosys design -reset
yosys read_verilog test/data/track.v
yosys hierarchy -top top
yosys proc
yosys opt
yosys techmap
yosys opt
yosys dfflibmap -liberty $nangate
yosys abc -liberty $nangate
yosys opensta_net -liberty $nangate -link
yosys opensta_net -protect
yosys opensta_net -sdc test/data/track.sdc
yosys opt_clean
yosys select -assert-min 1 t:\$barrier
yosys autoname
yosys select -assert-min 1 w:*netview_anchor*
yosys opensta_net -unprotect
yosys opt_clean -purge
yosys select -assert-none w:*netview_anchor* c:*netview_anchor*
