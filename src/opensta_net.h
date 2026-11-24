#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

#include "constraints.h"

namespace opensta_net {

using Yosys::RTLIL::Design;

struct YosysSta;
extern YosysSta *the_sta;

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
public:
	void capture();
	void sync(Design *design);
	void link(Design *design);
	void stop();
	void unprotect(Design *design);
private:
	void rebuild(Design *design);
	bool started_ = false; // -link ran, stop() ends it
};

struct YosysSta : public sta::Sta {
	std::vector<SdcObjectRef> refs; // refs[N] of token N, commands in ::ysta::captured
	CommandLoss loss;               // of the command the sync applies now
	Yosys::NetTracker tracker;

	Epochs epochs;
	std::string last_unbuildable_reason;

	void makeReport() override;
	void makeNetwork() override;
};

struct PassOptions {
	std::string eval_cmd, liberty_file, source_file, sdc_file;
	bool link = false, sync = false, unprotect = false;
};

bool tclEval(const std::string &script);
void loadDesign(Design *design, Tcl_Obj *libs, const std::string &verilog, const std::string &top);
int tclLoadDesign(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
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
