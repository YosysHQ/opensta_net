#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include <set>
#include <tcl.h>

#include "yosys_network.hh"

namespace opensta_net {

using Handles = std::vector<ysta::Handle>;

// what a ref that is a wire now gives, wire_as of ysta::resolve
enum class WireAs : uint8_t {
	None,   // cell nothing, pin its loads, stand-in leaf pins are a changed form
	Pins,   // loads, stand-in leaf pins are the same form
	Driven, // driven loads only, a constant is a loss
	Edges,  // wire edges into the loads
	Nets    // as None, a net that is pins now gives a net-only command nothing
};

// one per token of the last capture, see Epochs::capture
struct SdcObjectRef : Yosys::TrackedRef {
	std::string name;                              // at the capture, for loss texts
	bool was_start = false;                        // Pin, valid path start at the capture
	bool was_end = false;                          // Pin, valid path end at the capture
	bool was_hier = false;                         // Net or scope Pin at capture, leaf pins stand in for a gone scope
	bool was_net = false;                          // Net at capture
	bool isWire() const { return !loads.empty(); } // a wire or a constant now
	Handles objects(WireAs wire_as = WireAs::None) const;
};

ysta::YosysNetwork &staNetwork();

// loss of one command so far, ysta::loss reads and clears it
struct CommandLoss {
	bool empty_word = false; // a token word found no object, not run
	bool vacuous = false;    // new logic has no path of it, not run, no loss
	bool changed = false;    // a hier pin or net is its leaf pins now
	bool partial = false;    // a wire or other logic drives an old output of a cell
	std::string missing;     // names of refs with no object
};

void makeRefs(const ysta::TokenIndexes &tokens);
std::vector<Yosys::TrackedRef *> trackedRefs();
SdcObjectRef *tokenRef(std::string_view word);
std::string namedText(const std::string &text);
std::vector<std::string> tclWords(Tcl_Obj *list);
std::string objectName(const sta::Network *network, ysta::Handle handle);
std::string tclQuoted(const std::string &text);
std::string objectHandle(ysta::Handle handle);

enum class Direction : uint8_t { Backward, Forward }; // walk over timing edges
enum class PortSide : uint8_t { Inputs, Outputs };    // ports of an old cell
enum class PathSide : uint8_t { Start, End };
enum class Polarity : uint8_t { Same, Inverted };     // clock edge at a start

// bits of a ReachMask, what a pin reaches in the new logic
enum Reach : unsigned {
	ReachNothing = 0,
	ReachTarget = 1,   // a target pin, see check_reach
	ReachOtherEnd = 2, // another end, or a path that stops in the new logic
	ReachBoth = ReachTarget | ReachOtherEnd
};

using ReachMask = unsigned;
using PinSet = std::set<const sta::Pin *>;
using SeenStarts = std::set<std::pair<const sta::Pin *, Polarity>>;
using CheckPins = std::pair<const sta::Pin *, const sta::Pin *>; // clock pin, checked pin of a check edge

// result lists of ysta::moved_ends, in result order
struct MovedEnds {
	Tcl_Obj *kept = nullptr; // objects that stayed, a Tcl list
	Handles moved;           // pins behind new logic
	Handles ends;            // path ends after the moved pins
	Handles starts;          // starts the moved pins clock
	Handles inverted_starts; // starts behind inverting logic
};

// cut pins at the ends of an arc of a gone cell, and its cut so far
struct ArcCut {
	Handles from_pins;     // of -from
	Handles inputs;        // of each input
	Handles to_pins;       // of -to, an input for a check arc
	Handles outputs;       // of each output
	ReachMask check_reach; // with no -to, checks from the port are arcs
	Tcl_Obj *edges;        // edges to disable
	PinSet seen = {};      // pins cutArcPaths visited
	bool exact = true;     // no arc path needs an edge of other paths
};

int tclResolve(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int foundOrLost(Tcl_Interp *interp, Tcl_Obj *objects, bool empty_is_vacuous);
bool wrongArgCount(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], int min_args, int max_args, const char *usage);
void appendFound(const SdcObjectRef &ref, WireAs wire_as, Tcl_Obj *objects);
bool hasNoWireDriver(ysta::Handle load);
Tcl_Obj *handleList(const Handles &handles, Tcl_Obj *list = Tcl_NewListObj(0, nullptr));
int vacuous(Tcl_Interp *interp);
std::string arcPort(const SdcObjectRef &ref, const std::string &port);
Handles cutPins(const SdcObjectRef &ref, PortSide side, const std::string &port);
std::vector<sta::Edge *> edgesAt(const sta::Pin *pin, Direction direction);
void appendEdges(Tcl_Obj *list, const sta::Pin *from, const sta::Pin *to);
void appendWireEdges(Tcl_Obj *list, const sta::Pin *pin);
bool appendCheckEdges(ArcCut &arc);
bool appendArcEdges(Tcl_Obj *list, const SdcObjectRef &ref, const std::string &from, const std::string &to);
bool reachesOnly(const sta::Pin *pin, const Handles &targets, const Handles &ends);
void cutArcPaths(ArcCut &arc, const sta::Pin *pin);
ReachMask reachMask(const sta::Pin *pin, Direction direction, ReachMask check_reach, const Handles &targets, const Handles &ends, PinSet &seen);
int tclMovedArc(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
bool arcWords(const SdcObjectRef &ref, const std::vector<std::string> &options, std::string &from, std::string &to, Tcl_Obj *words);
bool isPathEnd(const sta::Pin *pin, PathSide side);
void addPathEnds(const sta::Pin *pin, Handles &ends);
void addStartsBehind(const sta::Pin *pin, Polarity polarity, MovedEnds &found, SeenStarts &seen);
int tclMovedEnds(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
void addMovedEnds(const SdcObjectRef &ref, PathSide side, MovedEnds &found, SeenStarts &seen);
int tclMissing(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int tclLoss(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
std::string lossReason(const CommandLoss &loss, bool failed, const std::string &error);
int resolveAndApply();

} // namespace opensta_net

#endif
