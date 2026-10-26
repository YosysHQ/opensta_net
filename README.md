# opensta-adapter

Yosys plugin that runs OpenSTA on the live design. OpenSTA reads a view of the design in memory, and after each pass every constraint is kept or reported lost.

### Build and test

    git submodule update --init --recursive
    nix-shell --run "make deps"
    nix-shell --run "make test"

### Use

    deps/yosys/build/yosys -m ./opensta_net.so -c run.tcl

with `run.tcl`:

    yosys read_verilog top.v
    yosys synth -top top
    yosys dfflibmap -liberty cells.lib
    yosys abc -liberty cells.lib
    yosys opensta_net -liberty cells.lib -link
    yosys opensta_net -sdc top.sdc
    report_checks
    yosys autoname
    report_checks
    yosys opensta_net -unprotect
    yosys write_verilog out.v

