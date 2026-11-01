## opensta_net

OpenSTA network adapter for Yosys

### usage

Build and run a tcl script like so:
```sh
nix-shell --run "make deps && make"
deps/yosys/build/yosys -m ./opensta_net.so -c run.tcl
```
An example tcl script may look like this:
```tcl
set lib cells.lib
yosys read_verilog top.v
yosys synth -top top
yosys dfflibmap -liberty $lib
yosys abc -liberty $lib
yosys opt_clean -purge

# link once, then constrain as in OpenSTA
yosys opensta_net -liberty $lib -link
create_clock -period 10 -name clk [get_ports clk]
set_input_delay 1 -clock clk [get_ports a]
report_checks

# run yosys passes
yosys flatten
yosys opt
report_checks

# cleanup after yosys
yosys opensta_net -unprotect
yosys write_verilog -noattr out.v
```
