// Tcl commands of the sync apply, see ::ysta::run and ::ysta::apply in trace.tcl
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
	static const char *const names[] = {"none", "pins", "driven", "edges", "nets", nullptr}; // WireAs order
	int index = int(WireAs::None);
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
		// a cell that is a wire now gives the command no object
		is_wire = is_wire || (ref->isWire() && wire_as != WireAs::Driven);
		appendFound(*ref, wire_as, objects);
	}
	Tcl_SetObjResult(interp, objects);
	return foundOrLost(interp, objects, is_wire);
}

// call after the result is set, a loss puts its Tcl error there
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

// min_args and max_args count words after the command name
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
	bool has_pin = ref.kind == SdcObjectRef::Kind::Pin && !ref.pins.empty();
	bool on_leaf_pins = has_pin && ref.pins[0]->inst->isLeaf();
	bool is_changed_form = ref.was_hier && on_leaf_pins && wire_as == WireAs::None;
	the_sta->loss.changed = the_sta->loss.changed || is_changed_form;
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

// port name now, "" when its pins are not on one cell
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

// pins at the cut side of one port, of each port of the side when port is ""
Handles cutPins(const SdcObjectRef &ref, PortSide side, const std::string &port)
{
	Handles found;
	for (const auto &[name, is_output, pins] : ref.port_cuts) {
		Handles cut = staNetwork().handles(pins);
		bool on_side = is_output == (side == PortSide::Outputs);
		bool is_wanted_port = port.empty() || name == port;
		if (on_side && is_wanted_port)
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
	Tcl_Interp *interp = the_sta->tclInterp();
	std::string from_handle = objectHandle(ysta::Handle(from));
	std::string to_handle = objectHandle(ysta::Handle(to));
	std::string get_edges = "::get_timing_edges -from " + from_handle + " -to " + to_handle;
	if (Tcl_EvalEx(interp, get_edges.c_str(), -1, TCL_EVAL_GLOBAL) == TCL_OK)
		Tcl_ListObjAppendList(nullptr, list, Tcl_GetObjResult(interp));
}

// a pin disable does not cut paths that end at the pin
void appendWireEdges(Tcl_Obj *list, const sta::Pin *pin)
{
	sta::Graph *graph = the_sta->ensureGraph();
	Direction direction = staNetwork().isDriver(pin) ? Direction::Forward : Direction::Backward;
	for (sta::Edge *edge : edgesAt(pin, direction)) {
		if (edge->isWire())
			appendEdges(list, edge->from(graph)->pin(), edge->to(graph)->pin());
	}
}

// a check arc is the check edges from what -from reaches to what -to reaches
// false when another port also reaches the clock or the checked pin of one
bool appendCheckEdges(ArcCut &arc)
{
	// reach ends at the outputs of the old cell
	PinSet from_reach, to_reach;
	for (ysta::Handle pin : arc.from_pins)
		reachMask(pin, Direction::Forward, ReachNothing, {}, arc.outputs, from_reach);
	for (ysta::Handle pin : arc.to_pins)
		reachMask(pin, Direction::Forward, ReachNothing, {}, arc.outputs, to_reach);
	sta::Graph *graph = the_sta->ensureGraph();
	std::set<CheckPins> checks;
	for (const sta::Pin *pin : from_reach) {
		for (sta::Edge *edge : edgesAt(pin, Direction::Forward)) {
			const sta::Pin *checked = edge->to(graph)->pin();
			if (edge->role()->isTimingCheck() && to_reach.count(checked))
				checks.insert({pin, checked});
		}
	}
	bool exact = true;
	for (const auto &[clock_pin, checked] : checks) {
		exact = exact && reachesOnly(clock_pin, arc.from_pins, arc.inputs);
		exact = exact && reachesOnly(checked, arc.to_pins, arc.inputs);
		appendEdges(arc.edges, clock_pin, checked);
	}
	return exact;
}

// edges that stand for an arc of a gone cell, they have just the arc paths
// false when an arc path has no such edge, the command is then lost
bool appendArcEdges(Tcl_Obj *list, const SdcObjectRef &ref, const std::string &from, const std::string &to)
{
	PortSide to_side = PortSide::Outputs;
	for (const auto &[port, is_output, pins] : ref.port_cuts) {
		if (!is_output && port == to)
			to_side = PortSide::Inputs; // a check arc
	}
	ReachMask check_reach = to.empty() ? ReachTarget : ReachOtherEnd;
	Handles from_pins = cutPins(ref, PortSide::Inputs, from);
	Handles inputs = cutPins(ref, PortSide::Inputs, "");
	Handles to_pins = cutPins(ref, to_side, to);
	Handles outputs = cutPins(ref, PortSide::Outputs, "");
	ArcCut arc = {
		.from_pins = from_pins,
		.inputs = inputs,
		.to_pins = to_pins,
		.outputs = outputs,
		.check_reach = check_reach,
		.edges = list,
	};
	if (to_side == PortSide::Inputs)
		return appendCheckEdges(arc);
	for (ysta::Handle pin : arc.from_pins)
		cutArcPaths(arc, pin);
	return arc.exact;
}

// no path into the pin comes from an end that is no target
bool reachesOnly(const sta::Pin *pin, const Handles &targets, const Handles &ends)
{
	PinSet seen;
	return reachMask(pin, Direction::Backward, ReachNothing, targets, ends, seen) == ReachTarget;
}

// cuts the arc paths through the pin, by its wire edges when it has just such paths, else after it
void cutArcPaths(ArcCut &arc, const sta::Pin *pin)
{
	if (!arc.exact || !arc.seen.insert(pin).second)
		return;
	PinSet seen;
	ReachMask after = reachMask(pin, Direction::Forward, arc.check_reach, arc.to_pins, arc.outputs, seen);
	if ((after & ReachTarget) == 0)
		return;
	if (after == ReachTarget && reachesOnly(pin, arc.from_pins, arc.inputs)) {
		appendWireEdges(arc.edges, pin);
		return;
	}
	// arc paths end at a -to pin or at a check with other paths, no pin there has just them
	std::vector<sta::Edge *> edges = edgesAt(pin, Direction::Forward);
	bool has_check = std::ranges::any_of(edges, &sta::TimingRole::isTimingCheck, &sta::Edge::role);
	bool at_to_pin = std::ranges::count(arc.to_pins, ysta::Handle(pin)) != 0;
	bool is_end = at_to_pin || (has_check && arc.check_reach == ReachTarget);
	arc.exact = !is_end;
	for (sta::Edge *edge : edges) {
		if (!is_end && !edge->role()->isTimingCheck())
			cutArcPaths(arc, edge->to(the_sta->graph())->pin());
	}
}

// a timing check counts as check_reach
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
	// a path that ends in the new logic is no arc path
	if (direction == Direction::Forward && !has_arc)
		mask |= ReachOtherEnd;
	return mask;
}

// a found cell has the port names a pass gave, ports not on one cell give the edges of appendArcEdges
int tclMovedArc(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 2, 2, "cell_word options"))
		return TCL_ERROR;
	const char *cell_word = Tcl_GetString(objv[1]);
	SdcObjectRef *ref = tokenRef(cell_word);
	if (ref == nullptr || ref->kind != SdcObjectRef::Kind::Instance) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s is no cell of the last capture", cell_word));
		return TCL_ERROR;
	}
	std::string from, to;
	Tcl_Obj *words = Tcl_NewListObj(0, nullptr);
	bool ports_on_cell = arcWords(*ref, tclWords(objv[2]), from, to, words);
	Tcl_Obj *objects = Tcl_NewListObj(0, nullptr);
	bool is_deleted = ref->gone && ref->instances.empty() && ref->loads.empty();
	bool has_exact_cut = true;
	if (is_deleted) {
		the_sta->loss.missing += " " + ref->name;
	} else if (!ref->instances.empty() && ports_on_cell) {
		handleList(staNetwork().handles(ref->instances), objects);
	} else {
		Tcl_SetListObj(words, 0, nullptr);
		has_exact_cut = appendArcEdges(objects, *ref, from, to);
	}
	Tcl_ListObjAppendElement(interp, words, objects);
	Tcl_SetObjResult(interp, words);
	if (has_exact_cut)
		return foundOrLost(interp, objects, !is_deleted);
	Tcl_SetObjResult(interp, Tcl_NewStringObj("the new logic has the paths of the arc only with other paths", -1));
	return TCL_ERROR;
}

// appends each option with its port now, from and to get the old ports, false when a port is not on one cell
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
	if (side == PathSide::Start)
		return sdc->isExceptionStartpoint(pin);
	return sdc->isExceptionEndpoint(pin);
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
		bool can_keep_polarity = edge->isWire() || edge->sense() != sta::TimingSense::negative_unate;
		bool can_flip_polarity = !edge->isWire() && edge->sense() != sta::TimingSense::positive_unate;
		if (can_keep_polarity)
			addStartsBehind(to, polarity, found, seen);
		if (can_flip_polarity)
			addStartsBehind(to, flipped, found, seen);
	}
}

// MovedEnds lists for ::ysta::moved_end_words, vacuous when only moved has objects
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
	Tcl_Obj *moved = handleList(found.moved);
	Tcl_Obj *ends = handleList(found.ends);
	Tcl_Obj *starts = handleList(found.starts);
	Tcl_Obj *inverted_starts = handleList(found.inverted_starts);
	Tcl_Obj *result_lists[] = {found.kept, moved, ends, starts, inverted_starts};
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
		bool is_moved_end = ref.kind == SdcObjectRef::Kind::Pin && was_end && !isPathEnd(handle, side);
		if (!is_moved_end) {
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

// the sync command goes on without the named object
int tclMissing(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 1, 1, "name"))
		return TCL_ERROR;
	std::string name = Tcl_GetString(objv[1]);
	the_sta->loss.missing += " " + name;
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
	if (!loss.missing.empty())
		return "applied without" + loss.missing;
	if (loss.changed)
		return "applied on the leaf pins";
	return "";
}

// count of lost or changed commands, clocks and library objects apply by name
int resolveAndApply()
{
	Tcl_Interp *interp = the_sta->tclInterp();
	the_sta->loss = {};
	the_sta->tracker.resolve(staNetwork().view(), trackedRefs());
	int lost = 0;
	bool applied = tclEval("::ysta::apply");
	bool has_count = applied && Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &lost) == TCL_OK;
	if (!has_count)
		log_cmd_error("opensta_net: sync: %s\n", Tcl_GetStringResult(interp));
	return lost;
}

} // namespace opensta_net
