YOSYS_SRC ?= deps/yosys
OPENSTA ?= deps/OpenSTA

YOSYS_BUILD := $(abspath $(YOSYS_SRC))/build
YOSYS_CONFIG := $(YOSYS_BUILD)/yosys-config
YOSYS := $(YOSYS_BUILD)/yosys
STA_ROOT := $(abspath $(OPENSTA))

STA_LIBS = $(STA_ROOT)/build/sta_swig.a $(STA_ROOT)/build/libOpenSTA.a
STA_INC = -I$(STA_ROOT)/include -I$(STA_ROOT)/include/sta -I$(STA_ROOT)/build/include/sta -I$(STA_ROOT)/search

SRCS = src/opensta_net.cc src/yosys_network.cc

HAVE_TOOLCHAIN := $(shell printf '#include <tcl.h>\n#include <zlib.h>\n' | $(CXX) -E -x c++ - > /dev/null 2>&1 && echo yes)

all: opensta_net.so

opensta_net.so: $(SRCS) src/opensta_net.h src/yosys_network.hh $(STA_LIBS)
ifneq ($(HAVE_TOOLCHAIN),yes)
	$(error tcl.h or zlib.h not found: build inside the toolchain shell, e.g. nix-shell --run "make $(MAKECMDGOALS)")
endif
	$(YOSYS_CONFIG) --build $@ $(SRCS) $(STA_INC) $(STA_LIBS) -lcudd -lz -ltcl

.PHONY: smoke
smoke: opensta_net.so
	$(YOSYS) -m ./opensta_net.so -p 'opensta_net -eval report_units'

.PHONY: test
test: opensta_net.so
	bash test/run.sh

.PHONY: clean
clean:
	rm -f opensta_net.so
	rm -f test/*_out.sdc test/*_out.v test/flat_[12].sdc test/uniq_same.v test/uniq_diff.v
