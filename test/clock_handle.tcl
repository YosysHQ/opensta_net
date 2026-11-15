# a journal command has the name of its clock, not a handle, a handle from before a rebuild is refused
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -name c -period 10 [get_ports clk]
set_input_delay -clock [get_clocks c] 1 [get_ports in]
# a clock on the pin of c deletes c, the new clock gets its memory
create_clock -name later -period 5 [get_ports clk]
check_eq [fail_capture add -wire w top] 0
check_eq [count_lines [written_sdc] {set_input_delay*}] 0
# a command with no query syncs first, so the next capture keeps it
yosys design -reset
link_top test/data/simple.v
create_clock -name virt -period 10
yosys add -wire dummy top
set_clock_uncertainty 0.5 virt
yosys add -wire dummy2 top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [llength [all_clocks]] 1
set held [get_clocks virt]
yosys add -wire dummy3 top
check_eq [catch {set_clock_transition 0.1 $held}] 1
