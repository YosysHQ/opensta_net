#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "kernel/yosys.h"
#include "kernel/nettrack.h"

#include <tcl.h>

#include <memory>

#include "sta/Property.hh"
#include "sta/ReportTcl.hh"
#include "sta/Sdc.hh"
#include "sta/Sta.hh"

#include "constraints.h"
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

// Sdc::clear keeps the net resistances, net voltages and wire edges of the old network
struct StaleSdc : public sta::Sdc {
	static void clear(sta::Sdc *sdc);
};

// no public call drops the property values of deleted objects
struct StaleProperties : public sta::Properties {
	static void keepOnly(sta::Properties &properties, const std::vector<const void *> &objects);
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
	struct ViewOwner {
		std::unique_ptr<Yosys::NetView> view = std::make_unique<Yosys::NetView>();
		std::vector<std::unique_ptr<Yosys::NetView>> retired_views;
	};

	struct EpochState {
		EpochConstraints constraints;
		std::vector<ysta::Token> tokens;
		Yosys::NetTracker tracker;
		Yosys::TrackMode track_mode = Yosys::TrackMode::Barriers; // until -unprotect
	};

	struct SyncState {
		bool autosync = false;
		bool running = false;
		bool rebuild_pending = false;
		std::string last_unbuildable_reason;
		size_t wrapped_pass_count = 0;
	};

	ysta::YosysNetwork *yosys_network = nullptr;
	ViewOwner views;
	EpochState epoch;
	SyncState sync;
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

struct SyncRunning {
	bool &running;
	explicit SyncRunning(bool &flag);
	~SyncRunning();
};

struct PassOptions {
	std::string eval_cmd, liberty_file, source_file, sdc_file;
	bool link = false, sync = false, autosync = false, protect = false, unprotect = false;
};

enum class SourceKind : uint8_t {
	Sdc,
	Tcl
};

// Tcl
bool tclEval(const std::string &script);

// epoch.cc: epoch end, rebuild, link
bool writeModeSdc(const std::string &mode, std::string &text);
bool writeAllModes(SdcTextByMode &result);
bool captureEpochConstraints();
void closeEpoch();
void openEpoch();
bool needsSync();
bool canBuild(Design *design);
void syncView(Design *design);
void rebuildAndReapply(Design *design);
void warnDroppedParasitics();
void warnSharedNames();
std::vector<const void *> lastingObjects();
void addLiberty(const sta::LibertyLibrary *library, std::vector<const void *> &objects);
void dropView();
bool linkView(Design *design, std::string &error);
void linkDesign(Design *design);

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
size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt);
void linkOrSync(Design *design, const PassOptions &opt);
void readLiberty(const std::string &file);
bool sourceFile(const std::string &file, SourceKind kind);
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
