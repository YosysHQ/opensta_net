#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include "kernel/yosys.h"
#include "kernel/nettrack.h"

#include <tcl.h>

#include <optional>

#include "yosys_network.hh"

namespace opensta_net {

using SdcTextByMode = Yosys::dict<std::string, std::string>;

// one sdc object token, the rewrite state is per sync
struct SdcObjectRef : Yosys::TrackedRef {
	std::string name;
	bool used_outside_through = false;
	std::vector<ysta::Handle> use_objects = {}; // objects of a clock use or an arc, else the found objects
	bool inside_handled = false;                // a rewrite took the leaf pins of a flattened hierarchical pin
	bool stillAsLeafPins() const;
	std::vector<ysta::Handle> objects() const; // the found objects of its kind
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

enum class PinChoice : uint8_t {
	FoundPins,
	ExactPins
};

enum class ThroughContext : uint8_t {
	OutsideThrough,
	AfterThrough
};

enum class ArcEnd : uint8_t {
	From,
	To
};

enum class RefMark : uint8_t {
	IsolateCell,
	KeepAsInstance
};

enum class ClockUse : uint8_t {
	Clock,
	GeneratedClockSource, // the -source pin of create_generated_clock
	SourceLatency,
	NetworkLatency
};

// one tcl word of a command
struct SdcWord {
	std::string text;
	WordKind kind = WordKind::Plain;
	std::vector<SdcWord> nested_words = {};
	std::vector<SdcObjectRef *> refs = {};
	PinChoice pin_choice = PinChoice::FoundPins;
};

struct SdcCommand {
	std::string mode;
	std::string text; // with tokens
	std::string log_text;
	std::vector<SdcWord> words;
};

// refs of a command that lost or changed their meaning
struct RefLosses {
	std::string missing; // found no object
	std::string moved;   // flatten moved them inside
	bool found = false;
	bool to_drivers = false; // -unprotect moved them to the drivers
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
	bool parseWords(const std::string &script, std::vector<SdcWord> &words);
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
bool isEdgePair(const std::vector<SdcWord> &words);
bool isGetterList(const std::vector<SdcWord> &words);
// SDC text by mode -> commands of words, getter tokens -> refs
void parseCommands(const SdcTextByMode &modes, const std::vector<ysta::Token> &tokens, EpochConstraints &into);
int appendTraceTexts(SdcTextByMode &modes, const char *list);
void joinName(std::vector<SdcWord> &words);
void markRefs(const std::vector<SdcWord> &words, RefMark mark);
bool tightness(const SdcCommand &command, double &tight);
void orderOverrides(std::vector<SdcCommand> &commands);
std::string objectName(const sta::Network *network, ysta::Handle handle);
std::vector<SdcObjectRef *> refsOf(const std::vector<SdcWord> &words);
std::string tclQuoted(const std::string &text);
std::string swigHandle(ysta::Handle handle);
bool rendersThroughPins(const SdcObjectRef &ref, ThroughContext context);
std::vector<ysta::Handle> objectsToRender(const SdcObjectRef &ref, ThroughContext context, PinChoice choice);
std::string refHandles(SdcObjectRef &ref, ThroughContext context, PinChoice choice);
std::string renderWords(const std::vector<SdcWord> &words, int &empty_getters, ThroughContext context);
std::string renderGetter(const SdcWord &word, int &empty_getters, ThroughContext context);

// rewrite
void rewriteWords(std::vector<SdcWord> &words);
void remapArcPorts(std::vector<SdcWord> &words);
void remapPort(SdcObjectRef &ref, const std::string &name, ArcEnd end, std::string &text);
void splitEndpoints(std::vector<SdcWord> &words);
void exactStartPins(std::vector<SdcWord> &words, size_t at);
void disableWireEdges(std::vector<SdcWord> &words);
void hierPinLoads(std::vector<SdcWord> &words);
bool anyBehindNewLogic(const SdcWord &word);
bool hasFlag(const std::vector<SdcWord> &words, const std::string &flag);
void applyClockPins(const std::vector<SdcWord> &words);
void setClockPins(const std::vector<SdcWord> &words, ClockUse use);
void pickClockPins(SdcObjectRef &ref, ClockUse use);
std::vector<ysta::Handle> loadsInside(const SdcObjectRef &ref);
void loadsBehindPins(const std::vector<const Yosys::Netlist::Pin *> &pins, std::vector<ysta::Handle> &loads);
void singleReferencePin(const std::vector<SdcWord> &words);
void quoteClocks(std::vector<SdcWord> &words);

// apply
int resolveAndApply();
bool applyCommand(const SdcCommand &command, std::string &reason);
RefLosses refLosses(const std::vector<SdcWord> &words);
bool onlyBarrierInstancePins(const SdcWord &word);
std::string swapInfoScript(const std::string &script);

} // namespace opensta_net

#endif
