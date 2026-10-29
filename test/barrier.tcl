# $barrier cells are transparent: the view links and times as OpenSTA's reader of the same netlist without them
source test/lib/check.tcl
yosys read_liberty -lib test/data/simple.lib
yosys read_verilog test/data/barrier.v
yosys prep -barriers -top top
yosys select -assert-min 5 {t:$barrier}
yosys opensta_net -liberty test/data/simple.lib -link
set body {
	create_clock -period 10 -name clk [get_ports clk]
	set_input_delay 1 -clock clk [get_ports {in bi*}]
	set_output_delay 1 -clock clk [get_ports {out bo*}]
	report_checks -path_delay min_max -fields {fanout cap slew} -digits 3
	report_wns -digits 3
	report_tns -digits 3
	puts "CELLS [lsort [lmap c [get_cells *] {get_full_name $c}]]"
	puts "NETS [lsort [lmap n [get_nets *] {get_full_name $n}]]"
	puts "LOADS [lsort [lmap p [get_pins -of_objects [get_nets n1]] {get_full_name $p}]]"
}
set got [sta_out $body]
check_eq [count_lines $got {*7.800   slack (MET)}] 1
yosys optbarriers -remove
yosys opt_clean
yosys select -assert-none {t:$barrier}
yosys write_verilog -noattr test/out/barrier.v
check_eq $got [reference "
	read_liberty test/data/simple.lib
	read_verilog test/out/barrier.v
	link_design top
	$body"]
