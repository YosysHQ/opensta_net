# 1025 x 1024 x 4096 pinless cells: more instance ids than 32 bits hold. the
# link refuses
set file [open test/id_overflow_out.v w]
puts $file "(* blackbox *) module fill(); endmodule"
puts $file "module leaf();"
for {set i 0} {$i < 4096} {incr i} { puts $file "  fill f${i}();" }
puts $file "endmodule\nmodule mid();"
for {set i 0} {$i < 1024} {incr i} { puts $file "  leaf l${i}();" }
puts $file "endmodule\nmodule top(input a, output y);\n  assign y = a;"
for {set i 0} {$i < 1025} {incr i} { puts $file "  mid m${i}();" }
puts $file "endmodule"
close $file
yosys read_verilog test/id_overflow_out.v
yosys hierarchy -top top
if {[catch {yosys opensta_net -link} msg]} { puts "REFUSED" } else { puts "LINKED [llength [get_cells {m1024/l1023/f4095 m0/l0/f4095}]]" }
