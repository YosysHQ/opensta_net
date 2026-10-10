puts "UNIQ_BEGIN"
puts "cells: [get_property [get_cells u1] ref_name] [get_property [get_cells u2] ref_name]"
puts "u1/b: [get_property [get_cells u1/b] ref_name] u2/b: [get_property [get_cells u2/b] ref_name]"
report_checks -unconstrained -to o1
report_checks -unconstrained -to o2
puts "UNIQ_END"
