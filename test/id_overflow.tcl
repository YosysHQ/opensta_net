# 1025 x 1024 x 4096 pinless cells: more instance ids than 32 bits hold, the
# link refuses
source test/lib/check.tcl
set file [open test/out/id_overflow.v w]
puts $file "(* blackbox *) module fill(); endmodule"
puts $file "module leaf();"
for {set i 0} {$i < 4096} {incr i} { puts $file "  fill f${i}();" }
puts $file "endmodule\nmodule mid();"
for {set i 0} {$i < 1024} {incr i} { puts $file "  leaf l${i}();" }
puts $file "endmodule\nmodule top(input a, output y);\n  assign y = a;"
for {set i 0} {$i < 1025} {incr i} { puts $file "  mid m${i}();" }
puts $file "endmodule"
close $file
yosys read_verilog test/out/id_overflow.v
yosys hierarchy -top top
set log [error_of {log_of opensta_net -link}]
check_eq [count_lines $log {ERROR: opensta_net: cannot link: the unfolded design needs more than the 32-bit object ids hold.}] 1
