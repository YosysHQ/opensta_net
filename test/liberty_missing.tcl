# A liberty file that cannot be read is a command error, not an abort.
if {[catch {yosys opensta_net -liberty test/no_such.lib} msg]} { puts "CAUGHT" }
puts "ALIVE"
