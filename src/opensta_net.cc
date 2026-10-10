#include "opensta_net.h"


#include "sta/Error.hh"
#include "sta/MinMax.hh"
#include "sta/StaMain.hh"

namespace sta {
extern const char *tcl_inits[];
}

extern "C" {
extern int Sta_Init(Tcl_Interp *interp);
}

USING_YOSYS_NAMESPACE

namespace opensta_net {

YosysSta *the_sta = nullptr;

void NonConsoleTee::operator()(LogSink &sink) const
{
	FILE *handle = sink.file_handle();
	if (handle != stdout && handle != stderr && sink.should_log(msg))
		sink.log(msg);
}

size_t YosysStaReport::printString(const char *buffer, size_t length)
{
	std::string_view text(buffer, length);
	LogMessage msg(LogSeverity::Info, {}, "", "%s", text);
	msg.cached_msg = std::string(text);
	logger().for_each_sink(NonConsoleTee{msg});
	return sta::ReportTcl::printString(buffer, length);
}

void YosysSta::makeReport()
{
	report_ = new YosysStaReport;
}

void YosysSta::makeNetwork()
{
	yosys_network = new ysta::YosysNetwork;
	network_ = yosys_network;
}

sta::Instance *YosysSta::makeInstance(const char *, sta::LibertyCell *, sta::Instance *)
{
	yosys_network->refuseEdit("make_instance");
	return nullptr;
}

void YosysSta::deleteInstance(sta::Instance *)
{
	yosys_network->refuseEdit("delete_instance");
}

void YosysSta::replaceCell(sta::Instance *, sta::Cell *, sta::LibertyCell *)
{
	yosys_network->refuseEdit("replace_cell");
}

sta::Net *YosysSta::makeNet(const char *, sta::Instance *)
{
	yosys_network->refuseEdit("make_net");
	return nullptr;
}

void YosysSta::deleteNet(sta::Net *)
{
	yosys_network->refuseEdit("delete_net");
}

void YosysSta::connectPin(sta::Instance *, sta::Port *, sta::Net *)
{
	yosys_network->refuseEdit("connect_pin");
}

void YosysSta::connectPin(sta::Instance *, sta::LibertyPort *, sta::Net *)
{
	yosys_network->refuseEdit("connect_pin");
}

void YosysSta::disconnectPin(sta::Pin *)
{
	yosys_network->refuseEdit("disconnect_pin");
}

void YosysSta::makePortPin(const char *, sta::PortDirection *)
{
	yosys_network->refuseEdit("make_port_pin");
}

bool tclEval(const std::string &script)
{
	return Tcl_Eval(yosys_get_tcl_interp(), script.c_str()) == TCL_OK;
}

void countScope(const Netlist::Instance *scope, size_t &insts, size_t &nets, size_t &pins)
{
	nets += the_sta->view.nets(scope).size();
	for (const Netlist::Instance *child : scope->children) {
		insts++;
		pins += child->pins.size();
		if (!child->leaf)
			countScope(child, insts, nets, pins);
	}
}

void logCounts(long ms)
{
	size_t insts = 0;
	size_t nets = 0;
	size_t pins = the_sta->view.top()->pins.size();
	countScope(the_sta->view.top(), insts, nets, pins);
	log("opensta_net: built network: %zu instances, %zu nets, %zu pins (%ld ms).\n", insts, nets, pins, ms);
}

void warnStubTypes()
{
	for (const std::string &type : the_sta->yosys_network->takeStubTypes())
		log_warning("opensta_net: no liberty cell for type %s, made a stub cell. Its instances contribute no "
		            "timing arcs.\n", type.c_str());
}

void dropView()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (network->linked()) {
		the_sta->networkChanged();
		network->unlink();
	}
	the_sta->view.reset();
}

bool linkView(Design *design, std::string &error)
{
	dropView();
	auto t0 = std::chrono::steady_clock::now();
	ysta::YosysNetwork *network = the_sta->yosys_network;
	network->attach(&the_sta->view);
	try {
		the_sta->view.build(design, network);
		network->link();
	} catch (const sta::Exception &exception) {
		network->unlink();
		the_sta->view.reset();
		error = exception.what();
		return false;
	}
	warnStubTypes();
	auto elapsed = std::chrono::steady_clock::now() - t0;
	logCounts(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
	return true;
}

void linkDesign(Design *design)
{
	std::string reason;
	if (!NetView::buildable(design, reason))
		log_cmd_error("opensta_net: cannot link: %s.\n", reason.c_str());
	if (!linkView(design, reason))
		log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
}

void makeSta()
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	sta::initSta();
	the_sta = new YosysSta;
	sta::Sta::setSta(the_sta);
	the_sta->makeComponents();
	the_sta->setTclInterp(interp);
	the_sta->setThreadCount(1);
	if (Sta_Init(interp) != TCL_OK)
		log_error("OpenSTA: Sta_Init failed: %s\n", Tcl_GetStringResult(interp));
	sta::evalTclInit(interp, sta::tcl_inits);
	if (!tclEval("init_sta_cmds"))
		log_error("OpenSTA: init_sta_cmds failed: %s\n", Tcl_GetStringResult(interp));
}

void bootSta()
{
	if (the_sta != nullptr)
		return;
	makeSta();
	log("OpenSTA instance created on the Yosys Tcl interpreter.\n");
}

size_t parseOptions(const std::vector<std::string> &args, Options &opt)
{
	const std::map<std::string, std::string *> values = {{"-eval", &opt.eval_cmd}, {"-liberty", &opt.liberty_file},
	                                                     {"-source", &opt.source_file}, {"-sdc", &opt.sdc_file}};
	const std::map<std::string, bool *> flags = {{"-link", &opt.link}};
	size_t argidx;
	for (argidx = 1; argidx < args.size(); argidx++) {
		const std::string &arg = args[argidx];
		auto value = values.find(arg);
		auto flag = flags.find(arg);
		if (value != values.end() && argidx + 1 < args.size())
			*value->second = args[++argidx];
		else if (flag != flags.end())
			*flag->second = true;
		else
			break;
	}
	opt.eval_cmd = unquote(opt.eval_cmd);
	return argidx;
}

void readLiberty(const std::string &file)
{
	if (the_sta->liberty_files_read.count(file)) {
		log("opensta_net: liberty file %s already loaded, skipping.\n", file.c_str());
		return;
	}
	sta::LibertyLibrary *lib = nullptr;
	try {
		lib = the_sta->readLiberty(file.c_str(), the_sta->cmdScene(), sta::MinMaxAll::all(), true);
	} catch (const sta::Exception &error) {
		log_cmd_error("OpenSTA: cannot read liberty file %s: %s\n", file.c_str(), error.what());
	}
	if (lib == nullptr)
		log_cmd_error("OpenSTA: failed to read liberty file %s.\n", file.c_str());
	the_sta->liberty_files_read.insert(file);
}

bool sourceFile(const std::string &file, bool sdc)
{
	std::string quoted = "{" + file + "}";
	if (sdc)
		return tclEval("read_sdc " + quoted);
	return tclEval("namespace eval ::sta {include_file " + quoted + " 0 0}");
}

void checkSourced(bool ok, const std::string &file)
{
	if (!ok)
		log_cmd_error("opensta_net: %s: %s\n", file.c_str(), Tcl_GetStringResult(yosys_get_tcl_interp()));
}

// A .ys line keeps the quotes of -eval "a b": drop one pair
std::string unquote(const std::string &arg)
{
	if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"')
		return arg.substr(1, arg.size() - 2);
	return arg;
}

void evalTcl(const std::string &cmd)
{
	bool ok = tclEval(cmd);
	const char *result = Tcl_GetStringResult(yosys_get_tcl_interp());
	if (!ok)
		log_cmd_error("OpenSTA tcl error: %s\n", result);
	if (result[0] != 0)
		log("%s\n", result);
}

OpenstaNetPass::OpenstaNetPass() : Pass("opensta_net", "OpenSTA network adapter") {}

void OpenstaNetPass::help()
{
	//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
	log("\n");
	log("    opensta_net [options]\n");
	log("\n");
	log("Boot an embedded OpenSTA instance on the Yosys Tcl interpreter over a netlist\n");
	log("view of the design. OpenSTA network edits (replace_cell, connect_pin, ...)\n");
	log("are refused, edit the design in yosys.\n");
	log("\n");
	log("    -liberty <file>\n");
	log("        read a liberty file into OpenSTA.\n");
	log("\n");
	log("    -link\n");
	log("        build the view from scratch (every module instantiated once: run\n");
	log("        uniquify or flatten first). Constraints are cleared.\n");
	log("\n");
	log("    -sdc <file>\n");
	log("        source an SDC file in the shared interpreter.\n");
	log("\n");
	log("    -source <file>\n");
	log("        source a Tcl file in the shared interpreter.\n");
	log("\n");
	log("    -eval <tcl>\n");
	log("        evaluate <tcl> in the shared interpreter and log the result.\n");
	log("\n");
}

void OpenstaNetPass::execute(std::vector<std::string> args, Design *design)
{
	log_header(design, "Executing OPENSTA_NET pass.\n");
	Options opt;
	size_t argidx = parseOptions(args, opt);
	extra_args(args, argidx, design);

	bootSta();
	if (!opt.liberty_file.empty())
		readLiberty(opt.liberty_file);
	if (opt.link)
		linkDesign(design);
	if (!opt.sdc_file.empty())
		checkSourced(sourceFile(opt.sdc_file, true), opt.sdc_file);
	if (!opt.source_file.empty())
		checkSourced(sourceFile(opt.source_file, false), opt.source_file);
	if (!opt.eval_cmd.empty())
		evalTcl(opt.eval_cmd);
}

OpenstaNetPass opensta_net_pass;

} // namespace opensta_net
