#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "kernel/yosys.h"
#include "kernel/netview.h"

#include <tcl.h>

#include <optional>

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

#include "yosys_network.hh"

namespace opensta_net {

using Yosys::RTLIL::Design;

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

// Constraints of one mode as SDC text
struct ModeSdc {
	std::string mode;
	std::string text;
};

struct YosysSta : public sta::Sta {
	ysta::YosysNetwork *yosys_network = nullptr;
	Yosys::NetView view;
	Yosys::pool<std::string> liberty_files_read;
	bool autosync = false;
	bool syncing = false;
	Yosys::Pass *seen_pass = nullptr;
	int seen_calls = -1;
	// Constraints of a sync whose rebuild failed
	std::optional<std::vector<ModeSdc>> held;
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
	bool link = false, sync = false, autosync = false;
};

// Tcl
bool tclEval(const std::string &script);
std::string normalizeCommand(const std::string &cmd);
std::vector<std::string> sdcCommands(const std::string &text);

// Build
void countScope(const Yosys::Netlist::Instance *scope, size_t &insts, size_t &nets, size_t &pins);
void logCounts(long ms);
void warnStubTypes();
void dropView();
bool linkView(Design *design, std::string &error);
void linkDesign(Design *design);

// Sync
bool writeModeSdc(const std::string &mode, std::string &text);
bool staleConstraints(std::vector<ModeSdc> &result);
int applyCommands(const std::vector<std::string> &commands);
int reapplyConstraints(const std::vector<ModeSdc> &before);
void warnDroppedParasitics();
bool canBuild(Design *design);
bool needsSync();
void rebuild(Design *design);
void syncView(Design *design);

// Tcl traces
int tclAfterYosys(ClientData, Tcl_Interp *, int, Tcl_Obj *const[]);
void notePassRun();

// Boot
void makeSta();
void wrapClockSense();
void traceLeave(const char *command, const char *handler);
void addTraces();
void bootSta();

// opensta_net
size_t parseOptions(const std::vector<std::string> &args, Options &opt);
void linkOrSync(Design *design, const Options &opt);
void readLiberty(const std::string &file);
bool sourceFile(const std::string &file, bool sdc);
void checkSourced(bool ok, const std::string &file);
std::string unquote(const std::string &arg);
void evalTcl(const std::string &cmd);

struct OpenstaNetPass : public Yosys::Pass {
	OpenstaNetPass();
	void help() override;
	void execute(std::vector<std::string> args, Design *design) override;
};

} // namespace opensta_net

#endif
