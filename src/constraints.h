#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include "kernel/yosys.h"
#include "kernel/nettrack.h"

#include <tcl.h>

#include <optional>

#include "yosys_network.hh"

namespace opensta_net {

using SdcTextByMode = Yosys::dict<std::string, std::string>;
using Handles = std::vector<ysta::Handle>;

// one per getter word object, see apply.cc
struct SdcObjectRef : Yosys::TrackedRef {
	std::string name;
	Handles objects() const; // the found objects of its kind
	void addObject(ysta::Handle handle);
};

ysta::YosysNetwork &staNetwork();

template <typename T> std::vector<ysta::Handle> handlesOf(const std::vector<T *> &records)
{
	return staNetwork().handles(records);
}

enum class WordKind : uint8_t {
	Plain,
	Getter,      // its refs
	Substitution // its nested words
};

// how a command uses the objects of a word, see assignUses
enum class Use : uint8_t {
	Objects,        // the found objects
	Through,        // after -through
	CaseValue,      // set_case_analysis
	Start,          // -from of a path command, -to of set_data_check
	Endpoint,       // -to of a path command
	ClockPin,       // the pins of a clock command
	ClockSource,    // -source of create_generated_clock
	SourceLatency,  // set_clock_latency -source
	NetworkLatency, // set_clock_latency without -source
	Disable,        // set_disable_timing
	ReferencePin    // -reference_pin
};

// which objects renderGetter takes, see objectsFor
enum class Pins : uint8_t {
	ByUse, // a -through net takes old_scope_pins
	Exact  // own_pins
};

enum class RefMark : uint8_t {
	IsolateCell,
	KeepAsInstance
};

// one tcl word of a command
struct SdcWord {
	std::string text;
	WordKind kind = WordKind::Plain;
	std::vector<SdcWord> nested_words = {};
	std::vector<SdcObjectRef *> refs = {};
	Use use = Use::Objects;
};
using SdcWords = std::vector<SdcWord>;

struct SdcCommand {
	std::string mode;
	std::string text; // with tokens
	std::string log_text;
	std::vector<SdcWord> words;
};

// a command rendered on the found objects, plus what it lost
struct RenderedCommand {
	std::string script;
	// a getter of only Barrier instance pins does not count
	bool empty_getter = false;  // a getter or clock getter rendered nothing
	bool refs_rendered = false; // a getter rendered a ref
	std::string missing;        // refs and clocks that found no object
	std::string moved;          // refs on a hierarchical pin flatten removed
	bool found = false;
	bool to_drivers = false;    // -unprotect moved refs to the drivers
};

// one per renderCommand
struct RenderState {
	RenderedCommand result = {};
	Yosys::pool<const SdcObjectRef *> on_stand_ins = {}; // refs with stand-ins, noteMovedRefs skips them
	std::vector<std::string> words_after_from = {};      // unbraced word after each -from, see renamedCells
};

// the constraints of the last epoch end
struct EpochConstraints {
	std::vector<SdcCommand> commands;
	std::deque<SdcObjectRef> refs;
	std::vector<Yosys::TrackedRef *> view_refs; // refs as the kernel takes them
};

// one parse into constraints, a getter makes a ref per known token
struct SdcReader {
	const std::vector<ysta::Token> &tokens;
	EpochConstraints &constraints;
	bool parseWords(const std::string &script, SdcWords &words);
	void parseSubstitution(const std::string &inner, SdcWord &word);
	void addGetterRefs(const std::string &inner, SdcWord &word);
	std::vector<ysta::Token> tokensIn(const std::string &text, std::string *names = nullptr) const;
	bool namesObject(const std::string &text) const;
};

// parse, tokens, render
std::vector<std::string> sdcCommands(const std::string &text);
std::string normalizeCommand(const std::string &cmd);
std::string commandName(const std::string &script);
bool isClockCommand(const std::string &name);
bool isThroughFlag(const std::string &word);
bool isEdgePair(const SdcWords &words);
bool isGetterList(const SdcWords &words);
// SDC text by mode -> commands of words, getter tokens -> refs
void parseCommands(const SdcTextByMode &modes, const std::vector<ysta::Token> &tokens, EpochConstraints &into);
int appendTraceTexts(SdcTextByMode &modes, const char *list);
void joinName(SdcWords &words);
void markRefs(const SdcWords &words, RefMark mark);
void assignUses(SdcWords &words, const std::string &command, bool latency_source);
void assignUse(SdcWords &words, Use use);
Use useOf(const std::string &command, const std::string &flag, bool latency_source);
bool tightness(const SdcCommand &command, double &tight);
void orderOverrides(std::vector<SdcCommand> &commands);
std::string objectName(const sta::Network *network, ysta::Handle handle);
std::vector<SdcObjectRef *> refsOf(const SdcWords &words);
std::string tclQuoted(const std::string &text);
std::string swigHandle(ysta::Handle handle);
Handles objectsFor(const SdcObjectRef &ref, Use use, Pins pins, const Handles &stand_ins, const Handles &found);
std::string refHandles(const SdcObjectRef &ref, const Handles &objects);
// script text on the found objects
std::string renderWord(const SdcWord &word, RenderState &render);
std::string renderWords(const SdcWords &words, RenderState &render);
std::string renderGetter(const SdcWord &word, Use use, Pins pins, RenderState &render);
std::string unwrapped(const std::string &text, char open, char close);

// rewrite
RenderedCommand renderCommand(const SdcCommand &command);
void noteMovedRefs(const SdcWords &words, RenderState &render);
std::string renamedPort(const std::vector<SdcObjectRef *> &refs, const std::string &text);
// cells of the new ports abc gave a -from arc port
Handles renamedCells(const SdcObjectRef &ref, const std::vector<std::string> &words_after_from);
std::optional<std::string> renderFlagAndGetterBehindNewLogic(const SdcWords &words, size_t at, RenderState &render);
std::string renderTopWord(const SdcWords &words, size_t at, RenderState &render);
// set_disable_timing on a moved pin or removed cell as wire edges, else ""
std::string disabledEdges(const SdcWord &word, RenderState &render);
bool anyBehindNewLogic(const SdcWord &word);
bool hasFlag(const SdcWords &words, const std::string &flag);
// objects that take a ref's use in place of its found objects
// e.g. -through moved output pin u1/y takes the loads beyond y
Handles standIns(const SdcObjectRef &ref, Use use, const Handles &found);
Handles loadsInside(const SdcObjectRef &ref);
void loadsBehindPins(const std::vector<const Yosys::Netlist::Pin *> &pins, Handles &loads);
std::string clockGetter(const SdcWord &word, RenderState &render); // "" when not a get_clocks word
void appendSpaced(std::string &text, const std::string &word);

// apply
int resolveAndApply();
bool applyCommand(const SdcCommand &command, std::string &reason);
bool onlyBarrierInstancePins(const SdcWord &word);
std::string swapInfoScript(const std::string &script);

} // namespace opensta_net

#endif
