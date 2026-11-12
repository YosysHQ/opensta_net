# track.sdc on a mapped design survives autoname, flatten and -unprotect
source test/lib/check.tcl
proc track {passes} {
	link_track
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
