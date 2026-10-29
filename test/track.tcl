# the constraints of track.sdc on a mapped design survive autoname, flatten
# and -unprotect: each sync loses none and reports none changed
source test/lib/check.tcl
proc track {passes} {
	yosys design -reset
	yosys read_verilog test/data/track.v
	yosys hierarchy -top top
	yosys proc
	yosys opt
	yosys techmap
	yosys opt
	yosys dfflibmap -liberty $::nangate
	yosys abc -liberty $::nangate
	yosys opensta_net -liberty $::nangate -link
	yosys opensta_net -sdc test/data/track.sdc
	set log {}
	foreach pass $passes { append log [log_of {*}$pass] }
	append log [log_of opensta_net -sync]
	return $log
}
set log [track {autoname}]
check_eq [sync_losses $log] 0
check_eq [lost_cmds $log] {}
set log [track {flatten}]
check_eq [sync_losses $log] 0
check_eq [lost_cmds $log] {}
set log [track {flatten {opensta_net -unprotect} {select -assert-none t:$barrier}}]
check_eq [sync_losses $log] {0 0}
check_eq [lost_cmds $log] {}
