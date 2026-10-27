#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include "kernel/yosys.h"
#include "kernel/netview.h"

#include <tcl.h>

#include "yosys_network.hh"

namespace opensta_net {

using ModeTexts = Yosys::dict<std::string, std::string>;

struct Ref : Yosys::TrackedRef {
	std::string name;
	bool plain = false;                 // used outside a -through
	std::vector<const void *> use = {}; // objects of a clock use, else objects
};

enum class ClockUse : uint8_t {
	Clock,
	SourceLatency,
	NetworkLatency
};

struct Word {
	std::string text;
	std::vector<Word> nested_words;
	std::vector<uint32_t> refs;
	bool nested = false;
};

struct Command {
	std::string mode;
	std::string text; // with tokens
	std::string log_text;
	std::vector<Word> words;
};

struct Constraints {
	std::vector<Command> commands;
	std::deque<Ref> refs;
	std::vector<Yosys::TrackedRef *> view_refs;
};

// parse, tokens, render
std::vector<std::string> sdcCommands(const std::string &text);
std::string normalizeCommand(const std::string &cmd);
std::string commandName(const std::string &script);
bool isClockCommand(const std::string &name);
bool isThroughFlag(const std::string &word);
bool parseWords(const std::string &script, std::vector<Word> &words);
void parseSubstitution(const std::string &inner, Word &word);
void parseGetter(const std::string &inner, Word &word);
void addJournal(ModeTexts &modes);
void markRefs(const std::vector<Word> &words, bool Ref::Request::*flag);
std::vector<ysta::Token> tokensIn(const std::string &text);
std::string untoken(const std::string &text);
std::string objectName(Ref::ObjectKind kind, const void *object);
std::string tokenName(const ysta::Token &token);
std::vector<uint32_t> refsOf(const std::vector<Word> &words);
std::string tclQuoted(const std::string &text);
std::string handleOf(Ref::ObjectKind kind, const void *object);
std::string refHandles(Ref &ref, bool after_through);
std::string render(const std::vector<Word> &words, int &empty_getters, bool after_through);

// rewrite
bool hasFlag(const std::vector<Word> &words, const std::string &flag);
void applyClockImages(const std::vector<Word> &words);
void clockImages(const std::vector<Word> &words, ClockUse use);
void clockImage(Ref &ref, ClockUse use, bool source);
std::vector<const void *> loadsInside(const Ref &ref);
void addInnerLoads(const std::vector<const void *> &pins, std::vector<const void *> &loads);

// apply
int applyConstraints();
bool applyCommand(const Command &command, std::string &reason);
bool keptMeaning(const std::vector<Word> &words, const std::string &moved, std::string &reason);
std::string movedInside(const std::vector<Word> &words);
bool anyUnprotected(const std::vector<Word> &words);
bool foundAny(const std::vector<Word> &words);
bool onlyImages(const Word &word);
std::string missingNames(const std::vector<Word> &words);
int tclInt(const std::string &script);

} // namespace opensta_net

#endif
