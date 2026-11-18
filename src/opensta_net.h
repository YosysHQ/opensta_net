#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

#include "constraints.h"

namespace opensta_net {

using Yosys::RTLIL::Design;

struct YosysSta;
extern YosysSta *the_sta;
bool queryHook();

// skips the console, the Tcl channel prints there
struct NonConsoleTee {
	const Yosys::LogMessage &msg;
	void operator()(Yosys::LogSink &sink) const;
};

struct YosysStaReport : public sta::ReportTcl {
	size_t printConsole(const char *buffer, size_t length) override;
};

// the adapter loop, end -> capture -> change -> query -> sync -> rebuild -> resolveAndApply
class Epochs {
	enum class Phase : uint8_t {
		Stopped, // no network until link
		Open,    // queries see the network
		Ended,   // maybe stale or unlinked, next query syncs
		Building // in link or sync, no nested sync
	};
public:
	void end();
	bool stale() const;
	bool syncForQuery(Design *design);
	void sync(Design *design);
	void link(Design *design);
	void unprotect(Design *design);
private:
	void capture();
	Phase rebuild(Design *design);
	Phase phase_ = Phase::Stopped;
};

struct YosysSta : public sta::Sta {
	std::vector<SdcObjectRef> refs; // refs[N] of token N, commands in ::ysta::captured
	CommandLoss loss;               // of the command the sync applies now
	Yosys::NetTracker tracker;
	bool track_barriers = true;     // -link or -protect until -unprotect, a $barrier or keep holds each ref
	bool autosync = false;          // sync after each Tcl edit command

	Epochs epochs;
	std::string last_unbuildable_reason;

	void makeReport() override;
	void makeNetwork() override;
};

struct PassOptions {
	std::string eval_cmd, liberty_file, source_file, sdc_file;
	bool link = false, sync = false, autosync = false, protect = false, unprotect = false;
};

bool tclEval(const std::string &script);
int tclBeforeEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
int tclAfterEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
bool topHasScopes(Design *design);
void beforePass(Yosys::Pass *pass, Design *design);
void makeSta();
void bootTcl();
void bootSta();
size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt);
void linkOrSync(Design *design, const PassOptions &opt);
void readLiberty(const std::string &file);
void sourceFile(const std::string &file, const std::string &command, const std::string &tail);
void evalTcl(const std::string &cmd);

bool canBuild(Design *design);
void warnDroppedParasitics();
void dropView();
bool linkView(Design *design, std::string &error);

struct OpenstaNetPass : public Yosys::Pass {
	OpenstaNetPass() : Pass("opensta_net", "OpenSTA network adapter") {}
	void help() override;
	void execute(std::vector<std::string> args, Design *design) override;
};

} // namespace opensta_net

#endif
