create_clock -period 10 -name clk [get_ports clk]
puts "linked before: [sta::network_is_linked]"
# a cell rename is Monitor-silent (a delete would still notify unsetPort). the
# pass start ends the epoch, so the next OpenSTA command reads a rebuilt
# network with the new name, not freed objects
yosys cd top
yosys rename u_inv1 u_x
yosys cd
puts "linked after: [sta::network_is_linked]"
set names {}
foreach cell [get_cells u_*] { lappend names [get_full_name $cell] }
puts "cells: [lsort $names]"
if {[catch {report_checks} msg]} { puts "refused: $msg" } else { puts "refused: NO" }
