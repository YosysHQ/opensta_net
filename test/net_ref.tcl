# a net ref stays through insbuf, that leaves its wire on a cell with no load
source test/lib/check.tcl
# the walk passes a buffer, an inverter is no buffer
foreach buf {BUF4 INV} {
	yosys design -reset
	yosys read_liberty -lib test/data/simple.lib
	yosys read_verilog test/data/net_alias.v
	yosys hierarchy -top top
	yosys flatten
	yosys setattr -set keep_name 1 w:i_cdc.i_dst.async_data_i
	yosys opensta_net -liberty test/data/simple.lib -link
	set_max_delay 5 -through [get_nets i_cdc.async_data]
	set lost {}
	foreach pass [list opt_clean "insbuf -buf $buf A Y" opt_clean {opt_clean -purge}] {
		yosys {*}$pass
		lappend lost {*}[lost_cmds [log_of opensta_net -sync]]
	}
	check_eq $lost {}
	check_eq [count_lines [written_sdc] {set_max_delay -through [get_nets {i_cdc.i_dst.async_data_i}] 5.0000}] 1
}
