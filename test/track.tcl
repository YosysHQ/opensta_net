# the constraints of test/track.sdc through the passes of $PASS. mapped to
# nangate45 first, so the gates have liberty cells
set lib deps/OpenSTA/test/nangate45/Nangate45_typ.lib
yosys read_verilog test/track.v
yosys hierarchy -top top
yosys proc
yosys opt
yosys techmap
yosys opt
yosys dfflibmap -liberty $lib
yosys abc -liberty $lib
yosys opensta_net -liberty $lib -link
yosys opensta_net -sdc test/track.sdc
foreach cmd [split $::env(PASS) ";"] {
	yosys {*}$cmd
}
yosys opensta_net -sync
