# write_sdc keeps the OpenSTA behavior: an unknown mode only warns, a directory gives the write error, a name that starts with | is a file, a failed write leaves no open channel
source test/lib/check.tcl
link_top test/data/simple.v
file delete test/out/nosuch.sdc
check_eq [catch {write_sdc -mode nosuch test/out/nosuch.sdc}] 0
write_sdc test/out/plain.sdc
check_eq [catch {write_sdc -gzip -mode nosuch test/out/plain.sdc}] 0
check_eq [error_of {write_sdc test}] {Error: cannot write file test.}
cd test/out
file delete piped
write_sdc {|touch piped}
check_eq [file exists piped] 0
file delete {|touch piped}
cd ../..
set channels [llength [file channels]]
catch {write_sdc /dev/full}
check_eq [llength [file channels]] $channels
