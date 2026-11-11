#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "kernel/yosys.h"
#include "kernel/nettrack.h"

#include <tcl.h>

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

#include "constraints.h"
#include "yosys_network.hh"

namespace opensta_net {

using Yosys::RTLIL::Design;

struct YosysSta;
extern YosysSta *the_sta; // made by makeSta

// logs to every sink but the console, the Tcl channel prints there
struct NonConsoleTee {
	const Yosys::LogMessage &msg;
	void operator()(Yosys::LogSink &sink) const;
};

struct YosysStaReport : public sta::ReportTcl {
	size_t printString(const char *buffer, size_t length) override;
};

// adapter loop, see epoch.cc
// end -> capture -> change -> query -> sync -> rebuild -> resolveAndApply
class Epochs {
	enum class Phase : uint8_t {
		Stopped,     // no network until link
		Open,        // queries see the network
		Ended,       // maybe stale, the next query syncs
		BuildFailed, // no network, the next query syncs
		Building     // in link or sync, no nested sync
	};
public:
	// before each OpenSTA query, true when queries see the network
	bool syncForQuery(Design *design);
	bool stale() const; // unlinked or older than the design, not when Stopped
	// Open -> capture -> Ended
	// before each pass but opensta_net, rtlil::set_attr and rtlil::set_param
	void end();
	// Open, Ended, BuildFailed -> Open, failed build -> BuildFailed
	// unbuildable Open -> Ended, throws -> Stopped
	void sync(Design *design);
	// any phase -> Open, unbuildable keeps it, failed build -> Stopped
	void link(Design *design);
	void unprotect(Design *design);
private:
	// write_sdc with object tokens, each anchors its rtlil objects
	bool capture();
	// link then resolveAndApply
	Phase rebuild(Design *design);
	Phase phase_ = Phase::Stopped;
};

struct YosysSta : public sta::Sta {
	ysta::YosysNetwork *yosys_network = nullptr;

	// from the last epoch end
	EpochConstraints constraints;
	// ref name per Barrier instance pin, set by sync, read by userName
	std::map<ysta::Handle, std::string> user_names;
	Yosys::NetTracker tracker;
	// until -unprotect a $barrier keeps each ref through passes
	bool track_barriers = true;

	// sync
	bool autosync = false;
	Epochs epochs;
	std::string last_unbuildable_reason;

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

struct PassOptions {
	std::string eval_cmd, liberty_file, source_file, sdc_file;
	bool link = false, sync = false, autosync = false, protect = false, unprotect = false;
};

// Tcl
bool tclEval(const std::string &script);

// see epoch.cc
bool writeModeSdc(const std::string &mode, std::string &text);
bool writeAllModes(SdcTextByMode &result);
bool canBuild(Design *design);
void warnDroppedParasitics();
void warnSharedNames(size_t shared);
void dropView();
bool linkView(Design *design, std::string &error);

// Tcl traces
bool queryHook();
int tclBeforeEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
int tclAfterYosys(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
int tclStaleNames(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

// boot
bool tclFailure(const std::string &what, std::string &error);
bool evalStaInit(Tcl_Interp *interp);
bool makeSta(std::string &error);
bool bootTcl(std::string &error);
void beforePass(Yosys::Pass *pass, Design *design);
void bootSta();

// opensta_net
size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt);
void linkOrSync(Design *design, const PassOptions &opt);
void readLiberty(const std::string &file);
void checkSourced(bool ok, const std::string &file);
std::string unquote(const std::string &arg);
void evalTcl(const std::string &cmd);
void setProtection(Design *design, const PassOptions &opt);

struct OpenstaNetPass : public Yosys::Pass {
	OpenstaNetPass();
	void help() override;
	void execute(std::vector<std::string> args, Design *design) override;
};

} // namespace opensta_net

#endif
