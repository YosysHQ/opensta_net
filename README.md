# opensta_net

OpenSTA network adapter for Yosys

## usage

> [!WARNING]
> Until merged, please use the Yosys in `deps`. Stock Yosys lacks the API this plugin needs.

Build and run a tcl script like so:
```sh
git submodule update --init --recursive
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

# link once, then constrain in the OpenSTA interpreter
yosys opensta_net -liberty $lib -link
yosys opensta_net -eval {
	create_clock -period 10 -name clk [get_ports clk]
	set_input_delay 1 -clock clk [get_ports a]
}
yosys opensta_net -eval report_checks

# run yosys passes
yosys flatten
yosys opt
yosys opensta_net -eval report_checks

# cleanup after yosys
yosys opensta_net -unprotect
yosys write_verilog -noattr out.v
```

## constraining

Link once after mapping, then constrain with an SDC file (`-sdc`), a Tcl file (`-source`) or a Tcl string (`-eval`):
```tcl
yosys opensta_net -liberty $lib -link
yosys opensta_net -sdc top.sdc
yosys opensta_net -eval report_checks
```
For all options, see `help opensta_net`. One call can take several options, they run in this order: liberty, link or sync, sdc, source, eval, unprotect.

## running passes

Run Yosys passes as usual. The constraints follow the design, and the next `opensta_net` call syncs to the new netlist first. If a pass removes a constrained object, that call warns:
```text
Warning: opensta_net: sync: `set_false_path -through [get_pins {u_a/Y}]` lost or changed: no object found for u_a/Y
```
To get the warning right at the pass that caused it, run `yosys opensta_net` after that pass. Edit the design only with Yosys commands. OpenSTA commands that edit the network fail, and `link_design` clears all constraints.

## writing the netlist

`-link` protects constrained objects with `$barrier` cells and `keep` attributes. Remove them before you write the netlist. `-unprotect` also ends the network and the constraints, so write the SDC first:
```tcl
yosys opensta_net -eval {write_sdc out.sdc} -unprotect
yosys write_verilog -noattr -norename out.v
```
`-norename` keeps the `$` names that the SDC uses.

## modules used more than once

Barriers go into the module, so a constraint on one instance slows down optimization of every instance. Run `flatten` or `uniquify` before `-link` to avoid this.

## running tests

```sh
bash test/run.sh         # all tests
bash test/run.sh flatten # one test
make test                # also the OpenSTA conformance suite
```
