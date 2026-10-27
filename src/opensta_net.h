#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "kernel/yosys.h"
#include "kernel/netview.h"

#include <tcl.h>

#include <memory>

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

#include "yosys_network.hh"

namespace opensta_net {

using Yosys::RTLIL::Design;

struct YosysSta;
extern YosysSta *the_sta;

// Feeds a message to every log sink but the console, which the Tcl channel
// serves, so STA and Tcl output keep their order there
struct NonConsoleTee {
	const Yosys::LogMessage &msg;
	void operator()(Yosys::LogSink &sink) const;
};

struct YosysStaReport : public sta::ReportTcl {
	int warnings = 0;
	size_t printString(const char *buffer, size_t length) override;
};

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

// track pass starts
struct EpochPass : public Yosys::Pass {
	Yosys::Pass *inner;
	explicit EpochPass(Yosys::Pass *inner);
	bool replace_existing_pass() const override;
	void help() override;
	bool formatted_help() override;
	void clear_flags() override;
	void on_shutdown() override;
	bool allow_garbage_collection_during_pass() const override;
	void execute(std::vector<std::string> args, Design *design) override;
};

struct YosysSta : public sta::Sta {
	ysta::YosysNetwork *yosys_network = nullptr;
	std::unique_ptr<Yosys::NetView> view = std::make_unique<Yosys::NetView>();
	std::vector<std::unique_ptr<Yosys::NetView>> retired_views;
	bool autosync = false;
	bool syncing = false;
	size_t wrapped_passes = 0;
	Constraints constraints;
	std::vector<ysta::Token> tokens;
	bool rebuild_pending = false;
	std::string unbuildable_reason;
	Yosys::Protection protection = Yosys::Protection::Barriers; // until -unprotect
public:
	void makeReport() override;
	void makeNetwork() override;
	sta::Instance *makeInstance(const char *name, sta::LibertyCell *cell, sta::Instance *parent) override;
	void deleteInstance(sta::Instance *inst) override;
	void replaceCell(sta::Instance *inst, sta::Cell *to_cell, sta::LibertyCell *to_lib_cell) override;
	sta::Net *makeNet(const char *name, sta::Instance *parent) override;
	void deleteNet(sta::Net *net) override;
	void connectPin(sta::Instance *inst, sta::Port *port, sta::Net *net) override;
	void connectPin(sta::Instance *inst, sta::LibertyPort *port, sta::Net *net) override;
	void disconnectPin(sta::Pin *pin) override;
	void makePortPin(const char *port_name, sta::PortDirection *dir) override;
};

struct Options {
	std::string eval_cmd, liberty_file, source_file, sdc_file;
	bool link = false, sync = false, autosync = false, protect = false, unprotect = false;
};

// Tcl
bool tclEval(const std::string &script);
std::string normalizeCommand(const std::string &cmd);
std::vector<std::string> sdcCommands(const std::string &text);

// Build
void warnSharedNames();
void dropView();
bool linkView(Design *design, std::string &error);
void linkDesign(Design *design);

// Epochs
bool writeModeSdc(const std::string &mode, std::string &text);
bool writeAllModes(ModeTexts &result);
void warnDroppedParasitics();
bool canBuild(Design *design);
bool needsSync();
void rebuild(Design *design);
void syncView(Design *design);
void closeEpoch();
void openEpoch();

// Constraints
bool parseWords(const std::string &script, std::vector<Word> &words);
void parseSubstitution(const std::string &inner, Word &word);
void parseGetter(const std::string &inner, Word &word);
std::vector<ysta::Token> tokensIn(const std::string &text);
std::string objectName(Ref::ObjectKind kind, const void *object);
std::string tokenName(const ysta::Token &token);
void addJournal(ModeTexts &modes);
std::string tclQuoted(const std::string &text);
std::string untoken(const std::string &text);
std::string commandName(const std::string &script);
std::vector<uint32_t> refsOf(const std::vector<Word> &words);
std::string handleOf(Ref::ObjectKind kind, const void *object);
std::string render(const std::vector<Word> &words, int &empty_getters, bool after_through);
bool isThroughFlag(const std::string &word);
std::string refHandles(Ref &ref, bool after_through);
void markRefs(const std::vector<Word> &words, bool Ref::Request::*flag);
bool injectConstraints();
bool isClockCommand(const std::string &name);
std::string movedInside(const std::vector<Word> &words);
bool anyUnprotected(const std::vector<Word> &words);
bool foundAny(const std::vector<Word> &words);
bool onlyImages(const Word &word);
std::string missingNames(const std::vector<Word> &words);
int tclInt(const std::string &script);
bool applyCommand(const Command &command, std::string &reason);
bool keptMeaning(const std::vector<Word> &words, const std::string &moved, std::string &reason);
bool hasFlag(const std::vector<Word> &words, const std::string &flag);
void applyClockImages(const std::vector<Word> &words);
void clockImages(const std::vector<Word> &words, ClockUse use);
void clockImage(Ref &ref, ClockUse use, bool source);
std::vector<const void *> loadsInside(const Ref &ref);
void addInnerLoads(const std::vector<const void *> &pins, std::vector<const void *> &loads);
int applyConstraints();

// Tcl traces
int tclBeforeEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
int tclAfterYosys(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);

// Boot
bool tclFailure(const std::string &what, std::string &error);
bool evalStaInit(Tcl_Interp *interp);
bool makeSta(std::string &error);
bool bootTcl(std::string &error);
void wrapPasses();
void bootSta();

// opensta_net
size_t parseOptions(const std::vector<std::string> &args, Options &opt);
void linkOrSync(Design *design, const Options &opt);
void readLiberty(const std::string &file);
bool sourceFile(const std::string &file, bool sdc);
void checkSourced(bool ok, const std::string &file);
std::string unquote(const std::string &arg);
void evalTcl(const std::string &cmd);
void setProtection(Design *design, const Options &opt);

struct OpenstaNetPass : public Yosys::Pass {
	OpenstaNetPass();
	void help() override;
	void execute(std::vector<std::string> args, Design *design) override;
};

} // namespace opensta_net

#endif
