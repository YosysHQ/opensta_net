#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include <set>
#include <tcl.h>

#include "yosys_network.hh"

namespace opensta_net {

using Handles = std::vector<ysta::Handle>;

// what a ref that is a wire now gives, the wire_as word of ysta::resolve
enum class WireAs : uint8_t {
	None,   // a pin gives its loads, a cell nothing, the leaf pins of a dissolved pin or net are a changed form
	Pins,   // the loads, the leaf pins are the same form
	Driven, // only the loads with a driver, a value on a constant is a loss
	Edges   // the wire edges into the loads
};

// one per token of the last capture, see Epochs::capture
struct SdcObjectRef : Yosys::TrackedRef {
	std::string name; // at the capture, for the loss texts
	bool was_start = false, was_end = false; // a Pin that was a valid path start or end at the capture
	bool was_hier = false; // a Net or a Pin of no leaf cell at the capture, leaf pins stand in when its scope is gone
	bool isWire() const { return !loads.empty(); } // a cell or leaf pin that is a wire or a constant now
	// the found objects of its kind, a wire is its loads, they have the paths through the old object, see WireAs
	Handles objects(WireAs wire_as = WireAs::None) const;
};

ysta::YosysNetwork &staNetwork(); // of the_sta

// what one command lost so far, ysta::loss reads and clears it
struct CommandLoss {
	bool empty_word = false; // a word of tokens found no object, the command did not run
	bool vacuous = false;    // the new logic has no path of the command, it did not run and is no loss
	bool changed = false;    // a hierarchical pin or a net of the command is its stand-in leaf pins now
	std::string missing;     // names of the refs that found no object
};

// refs
void makeRefs(const std::map<ysta::Handle, size_t> &tokens); // one ref per token at its index, replaces the old refs
std::vector<Yosys::TrackedRef *> trackedRefs();
SdcObjectRef *tokenRef(std::string_view word); // nullptr when not a token of the last capture
std::string namedText(const std::string &text); // tokens as the user names of their refs
std::vector<std::string> tclWords(Tcl_Obj *list);
std::string objectName(const sta::Network *network, ysta::Handle handle);
std::string tclQuoted(const std::string &text);
std::string objectHandle(ysta::Handle handle); // the SWIG object name, a pin of the top gives its port

// apply, the Tcl commands of ::ysta::run and ::ysta::apply in trace.tcl
enum class Direction : uint8_t { Backward, Forward }; // of a walk over the timing edges
enum class PortSide : uint8_t { Inputs, Outputs };    // of the ports of an old cell
enum class PathSide : uint8_t { Start, End };
enum class Polarity : uint8_t { Same, Inverted };     // of the clock edge at a start
// what a pin reaches in the new logic, a ReachMask has these bits
enum Reach : unsigned { ReachNothing = 0, ReachTarget = 1, ReachOtherEnd = 2, ReachBoth = ReachTarget | ReachOtherEnd };
using ReachMask = unsigned;
using PinSet = std::set<const sta::Pin *>;
using SeenStarts = std::set<std::pair<const sta::Pin *, Polarity>>;
// the lists of ysta::moved_ends in the order of its result
struct MovedEnds {
	Tcl_Obj *kept = nullptr;                    // the objects that stayed, a Tcl list
	Handles moved;                              // the pins behind new logic
	Handles ends;                               // the path ends after the moved pins
	Handles starts;                             // the starts that the moved pins clock
	Handles inverted_starts;                    // the starts behind inverting logic
};

bool hasNoWireDriver(ysta::Handle load); // no wire edge goes into the load
Tcl_Obj *handleList(const Handles &handles, Tcl_Obj *list = Tcl_NewListObj(0, nullptr)); // appends their SWIG names
int vacuous(Tcl_Interp *interp); // the Tcl error of a command with no path in the new logic
// after the result is set, TCL_OK with objects or an empty word of the user
// else notes the loss as vacuous or as an empty word and gives its Tcl error
int foundOrLost(Tcl_Interp *interp, Tcl_Obj *objects, bool empty_is_vacuous);
// true with the Tcl error of usage when a command has not min_args to max_args words after its name
bool wrongArgCount(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], int min_args, int max_args, const char *usage);
// ysta::resolve word ?wire_as?, the objects of a word of tokens, an error when it finds none
int tclResolve(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
// appends what a ref gives, see WireAs, and notes a ref with no object as missing
void appendFound(const SdcObjectRef &ref, WireAs wire_as, Tcl_Obj *objects);
// ysta::moved_ends option word, the lists of MovedEnds for ::ysta::moved_end_words, vacuous when only moved has objects
int tclMovedEnds(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
void addMovedEnds(const SdcObjectRef &ref, PathSide side, MovedEnds &found, SeenStarts &seen);
// ysta::moved_arc cell_word options, the words of an arc disable on the new network
// a found cell has the port names that a pass gave them, ports not on one cell give the pins of goneArcPins
int tclMovedArc(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
// appends each option with its port now, from and to get the old ports, false when a port is not on the one cell
bool arcWords(const SdcObjectRef &ref, const std::vector<std::string> &options, std::string &from, std::string &to,
              Tcl_Obj *words);
// the name of the port now, "" when the pins of the port are not on the one cell
std::string arcPort(const SdcObjectRef &ref, const std::string &port);
// the pins of the new logic that have just the paths of the arc, none when it has no path of the arc
//   the load pins of -from that reach -to, when none of them reaches another end
//   else the driver pins of -to that -from reaches, when no other port reaches them
//   else the load pins, both sides share arcs with other ports, so this also cuts the paths from -from to the
//   other outputs, a known limit, disable the edges on the paths when a cell needs it
Handles goneArcPins(const SdcObjectRef &ref, const std::string &from, const std::string &to);
// the cut pins of port on a path of the arc to far_port, a forward walk starts at the inputs
// the bool is false when one of them is also on another path
std::pair<Handles, bool> arcSide(const SdcObjectRef &ref, Direction direction, ReachMask check_reach,
                                 const std::string &port, const std::string &far_port);
// the pins at the cut side of the inputs or outputs, of one port or of each port when port is ""
Handles cutPins(const SdcObjectRef &ref, PortSide side, const std::string &port);
std::vector<sta::Edge *> edgesAt(const sta::Pin *pin, Direction direction); // out of its driver or into its load vertex
void appendEdges(Tcl_Obj *list, const sta::Pin *from, const sta::Pin *to); // the timing edges between two pins
void appendWireEdges(Tcl_Obj *list, const sta::Pin *pin); // cut the paths that end at the pin, a pin disable does not
// the edges that stand for an arc of a cell that is gone, see goneArcPins and appendCheckEdges
void appendArcEdges(Tcl_Obj *list, const SdcObjectRef &ref, const std::string &from, const std::string &to);
// a check arc is the check edges from what -from reaches to what -to reaches
// a known limit, a check that another port also reaches is disabled for it too
void appendCheckEdges(Tcl_Obj *list, const SdcObjectRef &ref, const std::string &from, const std::string &to);
// what the pin reaches in the new logic, a target or another of the ends, a timing check counts as check_reach
ReachMask reachMask(const sta::Pin *pin, Direction direction, ReachMask check_reach, const Handles &targets,
                    const Handles &ends, PinSet &seen);
bool isPathEnd(const sta::Pin *pin, PathSide side);
void addPathEnds(const sta::Pin *pin, Handles &ends); // the path ends in the fanout of the pin
// adds the first starts after the pin, those that an inverting arc clocks to inverted_starts
void addStartsBehind(const sta::Pin *pin, Polarity polarity, MovedEnds &found, SeenStarts &seen);
// ysta::loss failed error script, logs a lost or changed command, 1 when it is one
int tclLoss(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
// ysta::missing name, the command of the sync goes on without it
int tclMissing(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
std::string lossReason(const CommandLoss &loss, bool failed, const std::string &error); // "" when none
int resolveAndApply(); // how many commands are lost or changed, clocks and library objects apply by name

} // namespace opensta_net

#endif
