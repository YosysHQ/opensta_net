# opensta-adapter

Yosys plugin that runs OpenSTA on the live design.

```sh
git clone --recurse-submodules git@github.com:hexratcc/opensta-adapter.git
cd opensta-adapter
nix-shell --run "make deps"   # build yosys and OpenSTA in deps/
nix-shell --run "make"        # build opensta_net.so
nix-shell --run "make test"   # test/run.sh and conformance/run.sh
deps/yosys/build/yosys -m ./opensta_net.so -c run.tcl
```
