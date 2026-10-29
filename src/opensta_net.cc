#include "opensta_net.h"

#include "sta/Error.hh"
#include "sta/Liberty.hh"
#include "sta/MinMax.hh"
#include "sta/Scene.hh"
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
	if (text.starts_with("Warning"))
		warnings++;
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
	yosys_network->setOpenEpochHook(openEpoch);
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

int tclBeforeEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[])
{
	closeEpoch();
	return TCL_OK;
}

EpochPass::EpochPass(Pass *inner) : Pass(inner->pass_name, inner->short_help, inner->location), inner(inner)
{
	experimental_flag = inner->experimental_flag;
	internal_flag = inner->internal_flag;
	call_counter = inner->call_counter;
	runtime_ns = inner->runtime_ns;
}

bool EpochPass::replace_existing_pass() const
{
	return true;
}

void EpochPass::help()
{
	inner->help();
}

bool EpochPass::formatted_help()
{
	return inner->formatted_help();
}

void EpochPass::clear_flags()
{
	inner->clear_flags();
}

void EpochPass::on_shutdown()
{
	inner->on_shutdown();
}

bool EpochPass::allow_garbage_collection_during_pass() const
{
	return inner->allow_garbage_collection_during_pass();
}

// Before each pass: the epoch ends
void EpochPass::execute(std::vector<std::string> args, Design *design)
{
	closeEpoch();
	inner->execute(std::move(args), design);
	// A plugin load adds passes
	if (pass_register.size() != the_sta->sync.wrapped_pass_count)
		wrapPasses();
}

int tclAfterYosys(ClientData, Tcl_Interp *, int, Tcl_Obj *const[])
{
	the_sta->views.view->recheck();
	if (the_sta->sync.autosync && yosys_design != nullptr)
		syncView(yosys_design);
	return TCL_OK;
}

bool tclFailure(const std::string &what, std::string &error)
{
	error = what + ": " + Tcl_GetStringResult(yosys_get_tcl_interp());
	return false;
}

bool evalStaInit(Tcl_Interp *interp)
{
	char *script = sta::unencode(sta::tcl_inits);
	bool ok = Tcl_Eval(interp, script) == TCL_OK;
	delete[] script;
	return ok;
}

bool makeSta(std::string &error)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	sta::initSta();
	the_sta = new YosysSta;
	sta::Sta::setSta(the_sta);
	the_sta->makeComponents();
	the_sta->setTclInterp(interp);
	the_sta->setThreadCount(1);
	if (Sta_Init(interp) != TCL_OK)
		return tclFailure("Sta_Init failed", error);
	if (!evalStaInit(interp))
		return tclFailure("the init scripts failed", error);
	if (!tclEval("init_sta_cmds"))
		return tclFailure("init_sta_cmds failed", error);
	return true;
}

bool bootTcl(std::string &error)
{
	static const char *script =
#include "trace.inc"
	    ;
	Tcl_Interp *interp = yosys_get_tcl_interp();
	Tcl_CreateObjCommand(interp, "ysta::before_edit", tclBeforeEdit, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::after_yosys", tclAfterYosys, nullptr, nullptr);
	if (tclEval(script))
		return true;
	error = Tcl_GetStringResult(interp);
	return false;
}

void wrapPasses()
{
	std::vector<Pass *> passes;
	for (const auto &[name, pass] : pass_register)
		if (dynamic_cast<EpochPass *>(pass) == nullptr)
			passes.push_back(pass);
	for (Pass *pass : passes)
		new EpochPass(pass);
	Pass::init_register();
	the_sta->sync.wrapped_pass_count = pass_register.size();
}

void bootSta()
{
	if (the_sta != nullptr)
		return;
	std::string error;
	if (!makeSta(error) || !bootTcl(error))
		log_error("OpenSTA: %s\n", error.c_str());
	wrapPasses();
	log("OpenSTA instance created on the Yosys Tcl interpreter.\n");
}

size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt)
{
	const std::map<std::string, std::string *> values = {{"-eval", &opt.eval_cmd}, {"-liberty", &opt.liberty_file},
	                                                     {"-source", &opt.source_file}, {"-sdc", &opt.sdc_file}};
	const std::map<std::string, bool *> flags = {{"-link", &opt.link}, {"-sync", &opt.sync},
	                                             {"-autosync", &opt.autosync}, {"-protect", &opt.protect},
	                                             {"-unprotect", &opt.unprotect}};
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

void linkOrSync(Design *design, const PassOptions &opt)
{
	if (opt.link) {
		linkDesign(design);
		return;
	}
	// Passes may have run since the last call
	the_sta->views.view->recheck();
	if (!opt.sync && needsSync())
		log_warning("opensta_net: design changed since link, rebuilding the STA network "
		            "and re-applying the constraints.\n");
	syncView(design);
}

void readLiberty(const std::string &file)
{
	if (the_sta->network()->findLibertyFilename(file) != nullptr) {
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
}

bool sourceFile(const std::string &file, SourceKind kind)
{
	std::string quoted = "{" + file + "}";
	if (kind == SourceKind::Sdc)
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

// the sync after -unprotect applies the constraints by the anchors only
void setProtection(Design *design, const PassOptions &opt)
{
	if (opt.protect)
		the_sta->epoch.protection = Protection::Barriers;
	if (!opt.unprotect)
		return;
	the_sta->epoch.protection = Protection::AnchorsOnly;
	the_sta->yosys_network->setEpochPhase(ysta::EpochPhase::Open);
	the_sta->views.view->unprotect(design);
	syncView(design);
}

OpenstaNetPass::OpenstaNetPass() : Pass("opensta_net", "OpenSTA network adapter") {}

void OpenstaNetPass::help()
{
	//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
	log("\n");
	log("    opensta_net [options]\n");
	log("\n");
	log("Boot an embedded OpenSTA instance on the Yosys Tcl interpreter and keep its\n");
	log("netlist view in step with the design. OpenSTA network edits (replace_cell,\n");
	log("connect_pin, ...) are refused, edit the design in yosys.\n");
	log("\n");
	log("    -liberty <file>\n");
	log("        read a liberty file into OpenSTA.\n");
	log("\n");
	log("    -link\n");
	log("        build the view from scratch. A module used more than once gets one\n");
	log("        set of objects per use. Constraints are cleared.\n");
	log("\n");
	log("    -sync\n");
	log("        if the design changed since the view was built, rebuild it and\n");
	log("        apply the constraints again. Every call and every OpenSTA query\n");
	log("        does this implicitly.\n");
	log("\n");
	log("    -protect\n");
	log("        put $barrier cells at the objects that the constraints name, before\n");
	log("        each pass (the default). Passes keep the barriers and the logic\n");
	log("        between them.\n");
	log("\n");
	log("    -unprotect\n");
	log("        remove the barriers (before write_verilog) and put no new ones.\n");
	log("\n");
	log("    -autosync\n");
	log("        also sync after every `yosys`, `rtlil::set_attr` or `rtlil::set_param`\n");
	log("        command run from Tcl.\n");
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
	PassOptions opt;
	size_t argidx = parseOptions(args, opt);
	extra_args(args, argidx, design);

	bootSta();
	if (opt.autosync)
		the_sta->sync.autosync = true;
	if (!opt.liberty_file.empty())
		readLiberty(opt.liberty_file);
	linkOrSync(design, opt);
	if (!opt.sdc_file.empty())
		checkSourced(sourceFile(opt.sdc_file, SourceKind::Sdc), opt.sdc_file);
	if (!opt.source_file.empty())
		checkSourced(sourceFile(opt.source_file, SourceKind::Tcl), opt.source_file);
	if (!opt.eval_cmd.empty())
		evalTcl(opt.eval_cmd);
	setProtection(design, opt);
}

OpenstaNetPass opensta_net_pass;

} // namespace opensta_net
