# verilog and sdc handoff: OpenSTA's own reader of the write_verilog and
# write_sdc output reports the timing of the view
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/simple.v
yosys hierarchy -top top
yosys chtype -set INV_SLOW top/u_inv1
yosys opensta_net -liberty test/data/simple.lib -link
yosys opensta_net -sdc test/data/simple.sdc
write_sdc -no_timestamp test/out/handoff.sdc
set got [sta_out report_checks]
check_eq [count_lines $got {*8.20   slack (MET)}] 1
yosys write_verilog -noattr test/out/handoff.v
check_eq $got [reference {
	read_liberty test/data/simple.lib
	read_verilog test/out/handoff.v
	link_design top
	read_sdc test/out/handoff.sdc
	report_checks
}]
