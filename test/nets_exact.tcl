# exact get_nets finds the same nets as the wildcard scan
source test/lib/check.tcl
set count 20000
set file [open test/out/nets_exact.v w]
puts $file "module top(input in, output out);"
for {set i 0} {$i < $count} {incr i} { puts $file "  wire n$i;" }
puts $file "  INV u0 (.A(in), .Y(n0));"
for {set i 1} {$i < $count} {incr i} { puts $file "  INV u$i (.A(n[expr {$i - 1}]), .Y(n$i));" }
puts $file "  INV ul (.A(n[expr {$count - 1}]), .Y(out));"
puts $file "endmodule"
close $file
link_top test/out/nets_exact.v
set nets [lmap net [get_nets *] {get_full_name $net}]
check_eq [llength $nets] [expr {$count + 2}]
check_eq [join [lmap name $nets {get_full_name [get_nets $name]}] \n] [join $nets \n]
