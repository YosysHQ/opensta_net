#!/usr/bin/env bash
# run the OpenSTA network/, sdc/ and search/ tests on the design in yosys and
# compare the output with their .ok: bash conformance/run.sh [name...]
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
STA_ROOT="${STA_ROOT:-$ROOT/deps/OpenSTA}"
YOSYS="${YOSYS:-$ROOT/deps/yosys/build/yosys}"
PLUGIN="${PLUGIN:-$ROOT/opensta_net.so}"
OUT=$ROOT/conformance/out
# edit tests
refused=" network_modify network_deep_modify network_connected_pins network_cell_match_merge
	network_multi_lib network_leaf_iter network_hierarchy network_merge_bus_hier
	network_connect_liberty search_network_edit_deep search_network_edit_replace
	search_network_sta_deep search_search_arrival_required search_sta_extra search_write_sdf_model "
mkdir -p "$OUT"

fail=0
for tcl in "$STA_ROOT"/{network,sdc,search}/test/*.tcl; do
	name=$(basename "$tcl" .tcl)
	[ $# -eq 0 ] || [[ " $* " == *" $name "* ]] || continue
	(cd "$(dirname "$tcl")" && "$YOSYS" -m "$PLUGIN" -q -p "opensta_net -source $ROOT/conformance/load.tcl; opensta_net -source $name.tcl" < /dev/null) > "$OUT/$name.out" 2>&1
	code=$?
	if [[ $refused == *[[:space:]]$name[[:space:]]* ]]; then
		[ $code -ne 0 ] && grep -q "Error: 2929" "$OUT/$name.out" && echo "REFUSED $name" && continue
	elif [ $code -eq 0 ] && grep -v '^Warning [0-9]*:.*Creating black box' "${tcl%.tcl}.ok" | diff - "$OUT/$name.out" > "$OUT/$name.diff"; then
		echo "PASS $name"
		continue
	fi
	echo "FAIL $name (exit $code, $OUT/$name.*)"
	fail=$((fail + 1))
done
echo "$fail failed"
[ $fail -eq 0 ]
