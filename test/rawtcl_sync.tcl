# a constraint set from raw tcl after the last opensta_net call survives the
# next sync: the pass start writes the constraints as they are then
source test/lib/check.tcl
link_top test/data/sync.v
yosys opensta_net -sdc test/data/sync_ports.sdc
set_output_delay 2 -clock clk [get_ports out3]
yosys opt_merge -share_all
yosys opt_clean -purge
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [count_lines [written_sdc] {set_output_delay 2.0000 -clock [get_clocks {clk}] -add_delay [get_ports {out3}]}] 1
