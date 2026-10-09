# Exact get_nets is one lookup, not a scan of every net in the scope: the
# same nets as the wildcard scan, in about the time of exact get_cells.
set count 20000
set file [file tempfile path]
puts $file "module top(input in, output out);"
for {set i 0} {$i < $count} {incr i} { puts $file "  wire n$i;" }
puts $file "  INV u0 (.A(in), .Y(n0));"
for {set i 1} {$i < $count} {incr i} { puts $file "  INV u$i (.A(n[expr {$i - 1}]), .Y(n$i));" }
puts $file "  INV ul (.A(n[expr {$count - 1}]), .Y(out));"
puts $file "endmodule"
close $file
yosys read_liberty -lib test/simple.lib
yosys read_verilog $path
file delete $path
yosys hierarchy -top top
yosys opensta_net -liberty test/simple.lib -link
set nets {}
foreach net [get_nets *] { lappend nets [get_full_name $net] }
set found {}
foreach name $nets { lappend found [get_full_name [get_nets $name]] }
puts "EQUAL [expr {[llength $nets] > $count && $found eq $nets}]"
set probe [lrange $nets 0 1999]
set t0 [clock microseconds]
foreach name $probe { get_nets $name }
set net_us [expr {[clock microseconds] - $t0}]
set t0 [clock microseconds]
foreach name $probe { get_cells u[string range $name 1 end] }
set cell_us [expr {[clock microseconds] - $t0}]
puts "TIMES nets $net_us us cells $cell_us us"
puts "FAST [expr {$net_us < 5 * $cell_us + 20000}]"
