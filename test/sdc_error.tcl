# a broken sdc is a command error that tcl can catch. The run goes on, the
# constraints before the error stay, and the warnings name the right line
source test/lib/check.tcl
link_top test/data/sync.v
set log [error_of {log_of opensta_net -sdc test/data/sdc_error.sdc}]
check_eq [count_lines $log {Warning 361: sdc_error.sdc line 3, net 'no_such_net' not found.}] 1
check_eq [count_lines $log {Warning 472: sdc_error.sdc line 3, no valid objects specified for -through}] 1
check_eq [count_lines $log {ERROR: opensta_net: test/data/sdc_error.sdc: Error: sdc_error.sdc, 4 invalid command name "no_such_command"}] 1
check_eq [llength [all_clocks]] 1
