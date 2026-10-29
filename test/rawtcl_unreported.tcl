# an unreported change (a rename) before the optimization: the view is stale
# at the first reported change, the constraint set from raw tcl still
# carries over from the last epoch end
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -sdc test/data/sync_ports.sdc
set_output_delay 2 -clock clk [get_ports out3]
yosys cd top
yosys rename e1 e1_renamed
yosys cd ..
yosys opt_merge -share_all
yosys opt_clean -purge
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [count_lines [written_sdc] {set_output_delay 2.0000 -clock [get_clocks {clk}] -add_delay [get_ports {out3}]}] 1
