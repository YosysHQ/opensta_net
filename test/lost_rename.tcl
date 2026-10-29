# a rebuild that finds a constraint by a name that is now an alias loses
# nothing: a loss is a command that fails or warns, not a changed text
source test/lib/check.tcl
link_top test/data/names.v
set_load 0.01 [get_nets w1]
yosys add -wire w0 1 top
yosys setattr -set keep 1 w:w0
yosys connect -nomap -set w0 w1 top
check_eq [sync_losses [log_of opensta_net -sync]] 0
check_eq [get_full_name [get_nets w1]] w0
