# a net ref stays through insbuf, that leaves its wire on a cell with no load, and through a moved driver
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
	foreach pass [list opt_clean "insbuf -buf $buf A Y" opt_clean {opensta_net -unprotect} {opt_clean -purge}] {
		yosys {*}$pass
		lappend lost {*}[lost_cmds [log_of opensta_net -sync]]
	}
	check_eq $lost {}
	check_eq [count_lines [written_sdc] {set_max_delay -through [get_nets {i_cdc.i_dst.async_data_i}] 5.0000}] 1
}
# an unprotected pass moves the driver to another net, the old wire gets a new driver and names the net
yosys design -reset
link_top test/data/simple.v
set_max_delay 5 -through [get_nets n1]
yosys opensta_net -unprotect
yosys cd top
yosys connect -port u_inv1 Y d0
yosys connect -port u_inv0 Y n1
yosys cd
check_eq [lost_cmds [log_of opensta_net -sync]] {}
check_eq [count_lines [written_sdc] {set_max_delay -through [get_nets {n1}] 5.0000}] 1
