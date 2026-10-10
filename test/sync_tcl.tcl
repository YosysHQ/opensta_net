# Optimizations run from Tcl between STA queries: with -autosync every
# `yosys` command leaves the STA view in step, constraints included.
puts "SLACK [format %.2f [worst_slack -max]]"
yosys opt_merge -share_all
yosys opt_clean -purge
puts "SLACK [format %.2f [worst_slack -max]]"
puts "CELLS [llength [get_cells *]]"
