# a cell rename (no Monitor event) ends the epoch at the pass start, the next
# query reads a rebuilt network with the new name, not freed objects
source test/lib/check.tcl
link_top test/data/simple.v
create_clock -period 10 -name clk [get_ports clk]
check_eq [sta::network_is_linked] 1
yosys cd top
yosys rename u_inv1 u_x
yosys cd
check_eq [sta::network_is_linked] 1
check_eq [lsort [lmap cell [get_cells u_*] {get_full_name $cell}]] {u_dff0 u_inv0 u_inv2 u_x}
check_eq [count_lines [sta_out report_checks] {No paths found.}] 1
