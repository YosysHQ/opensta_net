# a kept command names its clock, a new clock in the memory of a deleted one does not get it
# a handle from before a rebuild is refused
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -name c -period 10 [get_ports clk]
set_input_delay -network_latency_included -clock [get_clocks c] 1 [get_ports in]
# a clock on the pin of c deletes c, the new clock gets its memory
create_clock -name later -period 5 [get_ports clk]
yosys add -wire w top
set log [log_of opensta_net -sync]
check_eq [sync_losses $log] 1
check_eq [count_lines $log "*lost or changed: no object found for c"] 1
check_eq [count_lines [written_sdc] {set_input_delay*}] 0
# each call syncs first, so its capture keeps a command with no query
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
# a handle of this network works, after a rebuild it is refused
set held [get_clocks virt]
set edge [get_timing_edges -from [get_pins u_inv1/A] -to [get_pins u_inv1/Y]]
set_disable_timing $edge
yosys add -wire dummy4 top
get_ports clk
check_eq [catch {set_clock_transition 0.1 $held}] 1
check_eq [catch {set_disable_timing $edge}] 1
set_clock_transition 0.1 [get_clocks virt]
