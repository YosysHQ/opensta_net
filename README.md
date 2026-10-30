### opensta_net

OpenSTA network adapter for Yosys

```sh
git clone --recurse-submodules git@github.com:YosysHQ/opensta_net.git
cd opensta_net
nix-shell --run "make deps"   # build yosys and OpenSTA in deps/
nix-shell --run "make"        # build opensta_net.so
nix-shell --run "make test"   # test/run.sh and conformance/run.sh
deps/yosys/build/yosys -m ./opensta_net.so -c run.tcl
```
