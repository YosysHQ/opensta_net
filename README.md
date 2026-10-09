# opensta-adapter

Yosys plugin that runs OpenSTA on the live design. It implements OpenSTA's `sta::Network` over the read-only Yosys `Netlist` interface (`kernel/netlist.h`) and re-applies the constraints after each design change.

    git submodule update --init --recursive
    (cd deps/yosys && nix develop -c sh -c "cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build")
    nix-shell --run "cmake -S deps/OpenSTA -B deps/OpenSTA/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POSITION_INDEPENDENT_CODE=ON && cmake --build deps/OpenSTA/build"
    nix-shell --run "make && make test"
