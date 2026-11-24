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

## constraining

Link once after mapping, then constrain with OpenSTA commands or an SDC file:
```tcl
yosys opensta_net -liberty $lib -link
yosys opensta_net -sdc top.sdc
```
For all options, see `help opensta_net`. One call can take several options, they run in this order: liberty, link or sync, sdc, source, eval, protect, unprotect.

## running passes

Run Yosys passes as usual. The constraints follow the design, and the next OpenSTA command sees the new netlist. If a pass removes a constrained object, that command warns:
```text
Warning: opensta_net: sync: `set_false_path -through [get_pins {u_a/Y}]` lost or changed: no object found for u_a/Y
```
To get the warning right at the pass that caused it, turn on `yosys opensta_net -autosync`. Edit the design only with Yosys commands. OpenSTA commands that edit the network fail, and `link_design` clears all constraints.

## writing the netlist

`-link` protects constrained objects with `$barrier` cells and `keep` attributes. Remove them before you write the netlist:
```tcl
yosys opensta_net -unprotect
yosys write_verilog -noattr -norename out.v
write_sdc out.sdc
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
