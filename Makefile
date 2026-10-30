YOSYS_SRC ?= deps/yosys
OPENSTA ?= deps/OpenSTA

YOSYS_BUILD := $(abspath $(YOSYS_SRC))/build
YOSYS_CONFIG := $(YOSYS_BUILD)/yosys-config
YOSYS := $(YOSYS_BUILD)/yosys
STA_ROOT := $(abspath $(OPENSTA))
YOSYS_HEADERS = $(wildcard $(YOSYS_BUILD)/share/include/kernel/*.h)

STA_LIBS = $(STA_ROOT)/build/sta_swig.a $(STA_ROOT)/build/libOpenSTA.a
STA_INC = -I$(STA_ROOT)/include -I$(STA_ROOT)/include/sta -I$(STA_ROOT)/build/include/sta

SRCS = src/opensta_net.cc src/yosys_network.cc src/epoch.cc src/sdc_words.cc src/rewrite.cc src/apply.cc

HAVE_TOOLCHAIN := $(shell printf '#include <tcl.h>\n#include <zlib.h>\n' | $(CXX) -E -x c++ - > /dev/null 2>&1 && echo yes)

all: opensta_net.so

opensta_net.so: $(SRCS) src/trace.inc src/opensta_net.h src/constraints.h src/yosys_network.hh $(STA_LIBS) $(YOSYS_HEADERS) Makefile
ifneq ($(HAVE_TOOLCHAIN),yes)
	$(error tcl.h or zlib.h not found: build inside the toolchain shell, e.g. nix-shell --run "make $(MAKECMDGOALS)")
endif
	$(YOSYS_CONFIG) --build $@ $(SRCS) $(STA_INC) $(STA_LIBS) -lcudd -lz -ltcl

src/trace.inc: src/trace.tcl
	{ printf 'R"TCL('; cat $<; printf ')TCL"\n'; } > $@

# yosys builds in its own nix shell
.PHONY: deps yosys opensta
deps: yosys opensta

yosys:
	cd $(YOSYS_SRC) && nix develop -c sh -c "test -f build/build.ninja || cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"
	cd $(YOSYS_SRC) && nix develop -c cmake --build build

opensta:
	test -f $(OPENSTA)/build/build.ninja || cmake -S $(OPENSTA) -B $(OPENSTA)/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POSITION_INDEPENDENT_CODE=ON
	cmake --build $(OPENSTA)/build

.PHONY: smoke
smoke: opensta_net.so
	$(YOSYS) -m ./opensta_net.so -p 'opensta_net -eval report_units'

.PHONY: test
test: opensta_net.so
	bash test/run.sh
	bash conformance/run.sh

.PHONY: clean
clean:
	rm -f opensta_net.so src/trace.inc
	rm -rf conformance/out test/out
