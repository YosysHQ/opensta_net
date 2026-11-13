// the Tcl commands of the sync apply, see ::ysta::run and ::ysta::apply in trace.tcl
#include "opensta_net.h"

#include "sta/Graph.hh"
#include "sta/Sdc.hh"
#include "sta/TimingRole.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

int tclResolve(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 1, 2, "word ?wire_as?"))
		return TCL_ERROR;
	static const char *const names[] = {"none", "pins", "driven", "edges", nullptr}; // in the order of WireAs
	int index = 0; // none without the word
	if (objc == 3 && Tcl_GetIndexFromObj(interp, objv[2], names, "wire_as", TCL_EXACT, &index) != TCL_OK)
		return TCL_ERROR;
	WireAs wire_as = WireAs(index);
	Tcl_Obj *objects = Tcl_NewListObj(0, nullptr);
	bool is_wire = false;
	for (const std::string &item : tclWords(objv[1])) {
		SdcObjectRef *ref = tokenRef(item);
		if (ref == nullptr) {
			Tcl_ListObjAppendElement(interp, objects, Tcl_NewStringObj(item.c_str(), -1));
			continue;
		}
		// a cell that is a wire now has nothing that the command can be on
		is_wire = is_wire || (ref->isWire() && wire_as != WireAs::Driven);
		appendFound(*ref, wire_as, objects);
	}
	Tcl_SetObjResult(interp, objects);
	return foundOrLost(interp, objects, is_wire);
}

int foundOrLost(Tcl_Interp *interp, Tcl_Obj *objects, bool empty_is_vacuous)
{
	int count = 0;
	Tcl_ListObjLength(interp, objects, &count);
	if (count != 0)
		return TCL_OK;
	if (empty_is_vacuous)
		return vacuous(interp);
	// an empty word of the user lost no object
	if (the_sta->loss.missing.empty())
		return TCL_OK;
	the_sta->loss.empty_word = true;
	Tcl_SetObjResult(interp, Tcl_NewStringObj("no object found", -1));
	return TCL_ERROR;
}

bool wrongArgCount(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], int min_args, int max_args, const char *usage)
{
	if (objc > min_args && objc <= max_args + 1)
		return false;
	Tcl_WrongNumArgs(interp, 1, objv, usage);
	return true;
}

void appendFound(const SdcObjectRef &ref, WireAs wire_as, Tcl_Obj *objects)
{
	Handles found = ref.objects(wire_as);
	if (ref.isWire() && wire_as == WireAs::Driven)
		std::erase_if(found, hasNoWireDriver);
	if (found.empty() && (!ref.isWire() || wire_as == WireAs::Driven))
		the_sta->loss.missing += " " + ref.name;
	bool on_leaf_pins = ref.kind == SdcObjectRef::Kind::Pin && !ref.pins.empty() && ref.pins[0]->inst->isLeaf();
	the_sta->loss.changed = the_sta->loss.changed || (ref.was_hier && on_leaf_pins && wire_as == WireAs::None);
	if (!ref.isWire() || wire_as != WireAs::Edges) {
		handleList(found, objects);
		return;
	}
	for (ysta::Handle load : found)
		appendWireEdges(objects, load);
}

bool hasNoWireDriver(ysta::Handle load)
{
	return std::ranges::none_of(edgesAt(load, Direction::Backward), &sta::Edge::isWire);
}

Tcl_Obj *handleList(const Handles &handles, Tcl_Obj *list)
{
	for (ysta::Handle handle : handles)
		Tcl_ListObjAppendElement(nullptr, list, Tcl_NewStringObj(objectHandle(handle).c_str(), -1));
	return list;
}

int vacuous(Tcl_Interp *interp)
{
	the_sta->loss.vacuous = true;
	Tcl_SetObjResult(interp, Tcl_NewStringObj("the new logic has no path of the command", -1));
	return TCL_ERROR;
}

std::string arcPort(const SdcObjectRef &ref, const std::string &port)
{
	bool has_cut = ref.gone;
	for (const auto &[old_port, is_output, pins] : ref.port_cuts) {
		has_cut = has_cut || old_port == port;
		for (const Netlist::Pin *pin : pins) {
			if (old_port == port && ref.instances.size() == 1 && pin->inst == ref.instances[0])
				return pin->port().name;
		}
	}
	return has_cut ? "" : port;
}

Handles cutPins(const SdcObjectRef &ref, PortSide side, const std::string &port)
{
	Handles found;
	for (const auto &[name, is_output, pins] : ref.port_cuts) {
		Handles cut = staNetwork().handles(pins);
		if (is_output == (side == PortSide::Outputs) && (port.empty() || name == port))
			found.insert(found.end(), cut.begin(), cut.end());
	}
	return found;
}

std::vector<sta::Edge *> edgesAt(const sta::Pin *pin, Direction direction)
{
	sta::Graph *graph = the_sta->ensureGraph();
	bool forward = direction == Direction::Forward;
	sta::Vertex *vertex = forward ? graph->pinDrvrVertex(pin) : graph->pinLoadVertex(pin);
	std::vector<sta::Edge *> edges;
	if (vertex == nullptr)
		return edges;
	std::unique_ptr<sta::VertexEdgeIterator> at;
	if (forward)
		at = std::make_unique<sta::VertexOutEdgeIterator>(vertex, graph);
	else
		at = std::make_unique<sta::VertexInEdgeIterator>(vertex, graph);
	while (at->hasNext())
		edges.push_back(at->next());
	return edges;
}

void appendEdges(Tcl_Obj *list, const sta::Pin *from, const sta::Pin *to)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	std::string from_handle = objectHandle(ysta::Handle(from)), to_handle = objectHandle(ysta::Handle(to));
	std::string get_edges = "::get_timing_edges -from " + from_handle + " -to " + to_handle;
	if (Tcl_EvalEx(interp, get_edges.c_str(), -1, TCL_EVAL_GLOBAL) == TCL_OK)
		Tcl_ListObjAppendList(nullptr, list, Tcl_GetObjResult(interp));
}

void appendWireEdges(Tcl_Obj *list, const sta::Pin *pin)
{
	sta::Graph *graph = the_sta->ensureGraph();
	Direction direction = staNetwork().isDriver(pin) ? Direction::Forward : Direction::Backward;
	for (sta::Edge *edge : edgesAt(pin, direction)) {
		if (edge->isWire())
			appendEdges(list, edge->from(graph)->pin(), edge->to(graph)->pin());
	}
}

void appendCheckEdges(Tcl_Obj *list, const SdcObjectRef &ref, const std::string &from, const std::string &to)
{
	// the reach ends at the outputs of the old cell
	Handles outputs = cutPins(ref, PortSide::Outputs, "");
	PinSet from_reach, to_reach;
	for (ysta::Handle pin : cutPins(ref, PortSide::Inputs, from))
		reachMask(pin, Direction::Forward, ReachNothing, {}, outputs, from_reach);
	for (ysta::Handle pin : cutPins(ref, PortSide::Inputs, to))
		reachMask(pin, Direction::Forward, ReachNothing, {}, outputs, to_reach);
	sta::Graph *graph = the_sta->ensureGraph();
	std::set<std::pair<const sta::Pin *, const sta::Pin *>> checks;
	for (const sta::Pin *pin : from_reach) {
		for (sta::Edge *edge : edgesAt(pin, Direction::Forward)) {
			const sta::Pin *checked = edge->to(graph)->pin();
			if (edge->role()->isTimingCheck() && to_reach.count(checked))
				checks.insert({pin, checked});
		}
	}
	for (const auto &[clock_pin, checked] : checks)
		appendEdges(list, clock_pin, checked);
}

void appendArcEdges(Tcl_Obj *list, const SdcObjectRef &ref, const std::string &from, const std::string &to)
{
	bool is_check = false;
	for (const auto &[port, is_output, pins] : ref.port_cuts)
		is_check = is_check || (!is_output && port == to);
	if (is_check) {
		appendCheckEdges(list, ref, from, to);
		return;
	}
	for (ysta::Handle pin : goneArcPins(ref, from, to))
		appendWireEdges(list, pin);
}

ReachMask reachMask(const sta::Pin *pin, Direction direction, ReachMask check_reach, const Handles &targets,
                    const Handles &ends, PinSet &seen)
{
	if (std::ranges::count(targets, ysta::Handle(pin)))
		return ReachTarget;
	if (std::ranges::count(ends, ysta::Handle(pin)))
		return ReachOtherEnd;
	if (!seen.insert(pin).second)
		return ReachNothing;
	sta::Graph *graph = the_sta->ensureGraph();
	ReachMask mask = ReachNothing;
	bool has_arc = false;
	for (sta::Edge *edge : edgesAt(pin, direction)) {
		if (edge->role()->isTimingCheck()) {
			mask |= check_reach;
			continue;
		}
		sta::Vertex *next = direction == Direction::Forward ? edge->to(graph) : edge->from(graph);
		mask |= reachMask(next->pin(), direction, check_reach, targets, ends, seen);
		has_arc = true;
	}
	// a path that ends in the new logic is not a path of the arc
	if (direction == Direction::Forward && !has_arc)
		mask |= ReachOtherEnd;
	return mask;
}

Handles goneArcPins(const SdcObjectRef &ref, const std::string &from, const std::string &to)
{
	// with no -to the checks from the port are arcs of the command
	ReachMask check_reach = to.empty() ? ReachTarget : ReachOtherEnd;
	auto [arc_loads, loads_exact] = arcSide(ref, Direction::Forward, check_reach, from, to);
	if (loads_exact)
		return arc_loads;
	auto [arc_drivers, drivers_exact] = arcSide(ref, Direction::Backward, ReachNothing, to, from);
	return drivers_exact ? arc_drivers : arc_loads;
}

std::pair<Handles, bool> arcSide(const SdcObjectRef &ref, Direction direction, ReachMask check_reach,
                                 const std::string &port, const std::string &far_port)
{
	PortSide near_side = PortSide::Inputs, far_side = PortSide::Outputs;
	if (direction == Direction::Backward)
		std::swap(near_side, far_side);
	Handles targets = cutPins(ref, far_side, far_port);
	Handles far_pins = cutPins(ref, far_side, "");
	Handles arc_pins;
	bool exact = true;
	for (ysta::Handle pin : cutPins(ref, near_side, port)) {
		PinSet seen;
		ReachMask mask = reachMask(pin, direction, check_reach, targets, far_pins, seen);
		exact = exact && mask != ReachBoth;
		if ((mask & ReachTarget) != 0)
			arc_pins.push_back(pin);
	}
	return {arc_pins, exact};
}

int tclMovedArc(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 2, 2, "cell_word options"))
		return TCL_ERROR;
	SdcObjectRef *ref = tokenRef(Tcl_GetString(objv[1]));
	if (ref == nullptr || ref->kind != SdcObjectRef::Kind::Instance) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s is no cell of the last capture", Tcl_GetString(objv[1])));
		return TCL_ERROR;
	}
	std::string from, to;
	Tcl_Obj *words = Tcl_NewListObj(0, nullptr);
	bool ports_on_cell = arcWords(*ref, tclWords(objv[2]), from, to, words);
	bool on_cell = !ref->instances.empty() && ports_on_cell;
	Tcl_Obj *objects = Tcl_NewListObj(0, nullptr);
	bool is_deleted = ref->gone && ref->instances.empty() && ref->loads.empty();
	if (is_deleted) {
		the_sta->loss.missing += " " + ref->name;
	} else if (on_cell) {
		handleList(staNetwork().handles(ref->instances), objects);
	} else {
		Tcl_SetListObj(words, 0, nullptr);
		appendArcEdges(objects, *ref, from, to);
	}
	Tcl_ListObjAppendElement(interp, words, objects);
	Tcl_SetObjResult(interp, words);
	return foundOrLost(interp, objects, !is_deleted);
}

bool arcWords(const SdcObjectRef &ref, const std::vector<std::string> &options, std::string &from, std::string &to,
              Tcl_Obj *words)
{
	bool on_cell = true;
	for (size_t at = 0; at + 1 < options.size(); at += 2) {
		const std::string &option = options[at], &old_port = options[at + 1];
		if (option == "-from")
			from = old_port;
		else
			to = old_port;
		std::string port = arcPort(ref, old_port);
		on_cell = on_cell && !port.empty();
		Tcl_ListObjAppendElement(nullptr, words, Tcl_NewStringObj(option.c_str(), -1));
		Tcl_ListObjAppendElement(nullptr, words, Tcl_NewStringObj(port.c_str(), -1));
	}
	return on_cell;
}

bool isPathEnd(const sta::Pin *pin, PathSide side)
{
	const sta::Sdc *sdc = the_sta->cmdSdc();
	return side == PathSide::Start ? sdc->isExceptionStartpoint(pin) : sdc->isExceptionEndpoint(pin);
}

void addPathEnds(const sta::Pin *pin, Handles &ends)
{
	sta::PinSeq from = {pin};
	bool flat = true, endpoints_only = true, thru_disabled = true, thru_constants = true;
	int all_levels = 0;
	sta::PinSet fanout = the_sta->findFanoutPins(&from, flat, endpoints_only, all_levels, all_levels, thru_disabled,
	                                             thru_constants, the_sta->cmdMode());
	for (const sta::Pin *end : fanout) {
		if (isPathEnd(end, PathSide::End))
			ends.push_back(end);
	}
}

void addStartsBehind(const sta::Pin *pin, Polarity polarity, MovedEnds &found, SeenStarts &seen)
{
	if (!seen.insert({pin, polarity}).second)
		return;
	bool inverted = polarity == Polarity::Inverted;
	if (isPathEnd(pin, PathSide::Start)) {
		Handles &starts = inverted ? found.inverted_starts : found.starts;
		starts.push_back(pin);
		return;
	}
	Polarity flipped = inverted ? Polarity::Same : Polarity::Inverted;
	for (sta::Edge *edge : edgesAt(pin, Direction::Forward)) {
		if (edge->role()->isTimingCheck())
			continue;
		const sta::Pin *to = edge->to(the_sta->graph())->pin();
		// an arc with no one sense gives both edges
		if (edge->isWire() || edge->sense() != sta::TimingSense::negative_unate)
			addStartsBehind(to, polarity, found, seen);
		if (!edge->isWire() && edge->sense() != sta::TimingSense::positive_unate)
			addStartsBehind(to, flipped, found, seen);
	}
}

int tclMovedEnds(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 2, 2, "option word"))
		return TCL_ERROR;
	bool is_from = std::string_view(Tcl_GetString(objv[1])).ends_with("from");
	PathSide side = is_from ? PathSide::Start : PathSide::End;
	MovedEnds found = {.kept = Tcl_NewListObj(0, nullptr)};
	SeenStarts seen;
	for (const std::string &item : tclWords(objv[2])) {
		SdcObjectRef *ref = tokenRef(item);
		if (ref == nullptr)
			Tcl_ListObjAppendElement(interp, found.kept, Tcl_NewStringObj(item.c_str(), -1));
		else
			addMovedEnds(*ref, side, found, seen);
	}
	int kept_count = 0;
	Tcl_ListObjLength(interp, found.kept, &kept_count);
	Tcl_Obj *result_lists[] = {found.kept, handleList(found.moved), handleList(found.ends), handleList(found.starts),
	                           handleList(found.inverted_starts)};
	Tcl_SetObjResult(interp, Tcl_NewListObj(std::size(result_lists), result_lists));
	bool has_new_ends = !found.ends.empty() || !found.starts.empty() || !found.inverted_starts.empty();
	if (!found.moved.empty() && kept_count == 0 && !has_new_ends)
		return vacuous(interp);
	return TCL_OK;
}

void addMovedEnds(const SdcObjectRef &ref, PathSide side, MovedEnds &found, SeenStarts &seen)
{
	bool was_end = side == PathSide::Start ? ref.was_start : ref.was_end;
	for (ysta::Handle handle : ref.objects()) {
		if (ref.kind != SdcObjectRef::Kind::Pin || !was_end || isPathEnd(handle, side)) {
			handleList({handle}, found.kept);
			continue;
		}
		found.moved.push_back(handle);
		if (side == PathSide::Start)
			addStartsBehind(handle, Polarity::Same, found, seen);
		else
			addPathEnds(handle, found.ends);
	}
}

int tclMissing(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 1, 1, "name"))
		return TCL_ERROR;
	the_sta->loss.missing += std::string(" ") + Tcl_GetString(objv[1]);
	return TCL_OK;
}

int tclLoss(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 3, 3, "failed error script"))
		return TCL_ERROR;
	int failed = 0;
	if (Tcl_GetBooleanFromObj(interp, objv[1], &failed) != TCL_OK)
		return TCL_ERROR;
	std::string reason = lossReason(the_sta->loss, failed, Tcl_GetString(objv[2]));
	the_sta->loss = {};
	if (!reason.empty()) {
		std::string script = namedText(Tcl_GetString(objv[3]));
		log_warning("opensta_net: sync: `%s` lost or changed: %s\n", script.c_str(), reason.c_str());
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(!reason.empty()));
	return TCL_OK;
}

std::string lossReason(const CommandLoss &loss, bool failed, const std::string &error)
{
	if (loss.empty_word)
		return "no object found for" + loss.missing;
	if (loss.vacuous)
		return "";
	if (failed)
		return "failed: " + error;
	if (loss.missing.empty())
		return loss.changed ? "applied on the leaf pins" : "";
	return "applied without" + loss.missing;
}

int resolveAndApply()
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	the_sta->loss = {};
	the_sta->tracker.resolve(staNetwork().view(), trackedRefs());
	int lost = 0;
	if (!tclEval("::ysta::apply") || Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &lost) != TCL_OK)
		log_cmd_error("opensta_net: sync: %s\n", Tcl_GetStringResult(interp));
	return lost;
}

} // namespace opensta_net
