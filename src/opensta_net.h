#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

#include "constraints.h"

namespace opensta_net {

using Yosys::RTLIL::Design;

struct YosysSta;
extern YosysSta *the_sta; // made by makeSta
bool queryHook(); // Epochs::syncForQuery of the_sta

// logs to every sink but the console, the Tcl channel prints there
struct NonConsoleTee {
	const Yosys::LogMessage &msg;
	void operator()(Yosys::LogSink &sink) const;
};

struct YosysStaReport : public sta::ReportTcl {
	size_t printConsole(const char *buffer, size_t length) override; // redirected output is not console output
};

// adapter loop, see epoch.cc
// end -> capture -> change -> query -> sync -> rebuild -> resolveAndApply
class Epochs {
	enum class Phase : uint8_t {
		Stopped, // no network until link
		Open,    // queries see the network
		Ended,   // maybe stale or unlinked, the next query syncs
		Building // in link or sync, no nested sync
	};
public:
	bool syncForQuery(Design *design); // before each OpenSTA query, true when queries see the network
	bool stale() const; // unlinked or older than the design, not when Stopped
	// Open -> capture -> Ended, before each pass but opensta_net, rtlil::set_attr and rtlil::set_param
	void end();
	// Open, Ended -> Open, a failed build or an unbuildable design -> Ended, throws -> Stopped
	void sync(Design *design);
	void link(Design *design); // any phase -> Open, unbuildable keeps it, failed build -> Stopped
	void unprotect(Design *design);
private:
	void capture(); // write_sdc with object tokens, each anchors its rtlil objects
	Phase rebuild(Design *design); // link then resolveAndApply
	Phase phase_ = Phase::Stopped;
};

struct YosysSta : public sta::Sta {
	std::vector<SdcObjectRef> refs; // refs[N] is of token N of the last capture, its commands are ::ysta::captured
	CommandLoss loss; // of the command the sync applies now
	Yosys::NetTracker tracker;
	bool track_barriers = true; // from -link until -unprotect a $barrier or a keep holds each ref through passes
	bool autosync = false; // sync after each Tcl edit command

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

// see epoch.cc
bool canBuild(Design *design);
void warnDroppedParasitics();
void dropView();
bool linkView(Design *design, std::string &error);

int tclBeforeEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
int tclAfterEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);

void makeSta();
void bootTcl();
bool topHasScopes(Design *design); // a cell of the top is a module instance
void beforePass(Yosys::Pass *pass, Design *design);
void bootSta();

size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt);
void linkOrSync(Design *design, const PassOptions &opt);
void readLiberty(const std::string &file);
// evals command {file} tail, nothing when there is no file
void sourceFile(const std::string &file, const std::string &command, const std::string &tail);
void evalTcl(const std::string &cmd);

struct OpenstaNetPass : public Yosys::Pass {
	OpenstaNetPass() : Pass("opensta_net", "OpenSTA network adapter") {}
	void help() override;
	void execute(std::vector<std::string> args, Design *design) override;
};

} // namespace opensta_net

#endif
