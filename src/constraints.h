#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include "kernel/yosys.h"
#include "kernel/netview.h"

#include <tcl.h>

#include "yosys_network.hh"

namespace opensta_net {

using SdcTextByMode = Yosys::dict<std::string, std::string>;

// one sdc object token, the rewrite state is per sync
struct SdcObjectRef : Yosys::TrackedRef {
	std::string name;
	bool used_outside_through = false;
	std::vector<const void *> use_objects = {}; // objects of a clock use, else objects
	bool inside_handled = false;                // a rewrite took the leaf pins of a flattened hierarchical pin
	bool stillAsLeafPins() const;
};

enum class WordKind : uint8_t {
	Plain,
	Getter,      // its refs
	Substitution // its nested words
};

enum class ThroughContext : uint8_t {
	OutsideThrough,
	AfterThrough
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
	std::vector<uint32_t> refs = {};
};

struct SdcCommand {
	std::string mode;
	std::string text; // with tokens
	std::string log_text;
	std::vector<SdcWord> words;
};

// the constraints of the last epoch end
struct EpochConstraints {
	std::vector<SdcCommand> commands;
	std::deque<SdcObjectRef> refs;
	std::vector<Yosys::TrackedRef *> view_refs; // refs as the kernel takes them
};

// parse, tokens, render
std::vector<std::string> sdcCommands(const std::string &text);
std::string normalizeCommand(const std::string &cmd);
std::string commandName(const std::string &script);
bool isClockCommand(const std::string &name);
bool isThroughFlag(const std::string &word);
bool parseWords(const std::string &script, std::vector<SdcWord> &words);
void parseSubstitution(const std::string &inner, SdcWord &word);
void addGetterRefs(const std::string &inner, SdcWord &word);
void parseCommands(const SdcTextByMode &modes);
void appendTraceTexts(SdcTextByMode &modes, const char *var);
void markRefs(const std::vector<SdcWord> &words, bool SdcObjectRef::Request::*flag);
std::vector<ysta::Token> tokensIn(const std::string &text);
std::string tokensToNames(const std::string &text);
bool namesObject(const std::string &text);
std::string objectName(SdcObjectRef::ObjectKind kind, const void *object);
std::string tokenName(const ysta::Token &token);
std::vector<uint32_t> refsOf(const std::vector<SdcWord> &words);
std::string tclQuoted(const std::string &text);
std::string swigHandleOf(SdcObjectRef::ObjectKind kind, const void *object);
bool rendersThroughPins(const SdcObjectRef &ref, ThroughContext context);
const std::vector<const void *> &objectsToRender(const SdcObjectRef &ref, ThroughContext context);
std::string refHandles(SdcObjectRef &ref, ThroughContext context);
std::string renderWords(const std::vector<SdcWord> &words, int &empty_getters, ThroughContext context);
std::string renderGetter(const SdcWord &word, int &empty_getters, ThroughContext context);

// rewrite
bool hasFlag(const std::vector<SdcWord> &words, const std::string &flag);
void applyClockImages(const std::vector<SdcWord> &words);
void clockImages(const std::vector<SdcWord> &words, ClockUse use);
void pickClockImage(SdcObjectRef &ref, ClockUse use);
std::vector<const void *> loadsInside(const SdcObjectRef &ref);
void addInnerLoads(const std::vector<const void *> &pins, std::vector<const void *> &loads);

// apply
int resolveAndApply();
bool applyCommand(const SdcCommand &command, std::string &reason);
bool keptMeaning(const std::vector<SdcWord> &words, const std::string &moved, std::string &reason);
void appendName(std::string &names, const std::string &name);
std::string namesMovedInside(const std::vector<SdcWord> &words);
bool anyMovedToDrivers(const std::vector<SdcWord> &words);
bool foundAny(const std::vector<SdcWord> &words);
bool onlyImagePins(const SdcWord &word);
std::string missingNames(const std::vector<SdcWord> &words);
int tclInt(const std::string &script);

} // namespace opensta_net

#endif
