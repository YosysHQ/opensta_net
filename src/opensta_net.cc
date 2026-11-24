#include "opensta_net.h"

#include "sta/Error.hh"
#include "sta/StaMain.hh"

namespace sta { extern const char *tcl_inits[]; }
extern "C" int Sta_Init(Tcl_Interp *interp);

USING_YOSYS_NAMESPACE

namespace opensta_net {

YosysSta *the_sta = nullptr;

void NonConsoleTee::operator()(LogSink &sink) const
{
	FILE *handle = sink.file_handle();
	if (handle != stdout && handle != stderr && sink.should_log(msg))
		sink.log(msg);
}

// redirected output is not console output
size_t YosysStaReport::printConsole(const char *buffer, size_t length)
{
	std::string_view text(buffer, length);
	LogMessage msg(LogSeverity::Info, {}, "", "%s", text);
	msg.cached_msg = std::string(text);
	logger().for_each_sink(NonConsoleTee{msg});
	return sta::ReportTcl::printConsole(buffer, length);
}

void YosysSta::makeReport() { report_ = new YosysStaReport; }

void YosysSta::makeNetwork() { network_ = new ysta::YosysNetwork; }

bool tclEval(const std::string &script) { return Tcl_Eval(the_sta->tclInterp(), script.c_str()) == TCL_OK; }

void loadDesign(Design *design, Tcl_Obj *libs, const std::string &verilog, const std::string &top)
{
	// a failed load must not leave the old network to the queries of this call
	the_sta->epochs.stop();
	Pass::call(design, "design -reset");
	for (const std::string &lib : tclWords(libs))
		Pass::call(design, std::vector<std::string>{"read_liberty", "-lib", lib});
	Pass::call(design, std::vector<std::string>{"read_verilog", verilog});
	Pass::call(design, std::vector<std::string>{"hierarchy", "-top", top});
	Pass::call(design, "uniquify");
	the_sta->epochs.link(design);
}

// conformance only, the one place the design changes inside a call
// quiet as tee -q, the .ok files have no yosys output
int tclLoadDesign(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (wrongArgCount(interp, objc, objv, 3, 3, "libs verilog top"))
		return TCL_ERROR;
	std::ostringstream log_stream;
	auto log_scope = logger().sink_scope();
	logger().clear_original();
	// the error goes into the Tcl result
	logger().add_sink<StreamLogSink>(log_stream);
	auto guard = logger().error_throw_scope();
	try {
		loadDesign(yosys_get_design(), objv[1], Tcl_GetString(objv[2]), Tcl_GetString(objv[3]));
	} catch (const log_cmd_error_exception &) {
		std::string log_text = log_stream.str();
		size_t error_at = std::min(log_text.rfind("ERROR: "), log_text.size());
		std::string error = "ysta::load_design failed, " + log_text.substr(error_at);
		Tcl_SetObjResult(interp, Tcl_NewStringObj(error.c_str(), -1));
		return TCL_ERROR;
	}
	return TCL_OK;
}

void makeSta()
{
	// private and never deleted, OpenSTA keeps it
	Tcl_Interp *interp = Tcl_CreateInterp();
	// init.tcl only, no yosys commands
	if (Tcl_Init(interp) != TCL_OK)
		log_error("OpenSTA: Tcl_Init failed: %s\n", Tcl_GetStringResult(interp));
	sta::initSta();
	the_sta = new YosysSta;
	sta::Sta::setSta(the_sta);
	the_sta->makeComponents();
	the_sta->setTclInterp(interp);
	if (Sta_Init(interp) != TCL_OK)
		log_error("OpenSTA: Sta_Init failed: %s\n", Tcl_GetStringResult(interp));
	sta::evalTclInit(interp, sta::tcl_inits);
	if (!tclEval("init_sta_cmds"))
		log_error("OpenSTA: init_sta_cmds failed: %s\n", Tcl_GetStringResult(interp));
}

void bootTcl()
{
	static const char *script =
#include "trace.inc"
	    ;
	Tcl_Interp *interp = the_sta->tclInterp();
	Tcl_CreateObjCommand(interp, "ysta::resolve", tclResolve, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::moved_ends", tclMovedEnds, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::moved_arc", tclMovedArc, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::missing", tclMissing, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::loss", tclLoss, nullptr, nullptr);
	if (getenv("OPENSTA_NET_CONFORMANCE") != nullptr)
		Tcl_CreateObjCommand(interp, "ysta::load_design", tclLoadDesign, nullptr, nullptr);
	char *stale_net_names = reinterpret_cast<char *>(staNetwork().staleNetNamesFlag());
	Tcl_LinkVar(interp, "::ysta::writing_sdc", stale_net_names, TCL_LINK_BOOLEAN);
	if (!tclEval(script))
		log_error("OpenSTA: %s\n", Tcl_GetStringResult(interp));
}

void bootSta()
{
	if (the_sta != nullptr)
		return;
	makeSta();
	bootTcl();
	log("OpenSTA instance created on a private Tcl interpreter.\n");
}

size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt)
{
	const std::map<std::string, std::string *> values = {
		{"-eval", &opt.eval_cmd},
		{"-liberty", &opt.liberty_file},
		{"-source", &opt.source_file},
		{"-sdc", &opt.sdc_file},
	};
	const std::map<std::string, bool *> flags = {
		{"-link", &opt.link},
		{"-unprotect", &opt.unprotect},
	};
	size_t argidx;
	for (argidx = 1; argidx < args.size(); argidx++) {
		const std::string &arg = args[argidx];
		if (values.count(arg) && argidx + 1 < args.size())
			*values.at(arg) = args[++argidx];
		else if (flags.count(arg))
			*flags.at(arg) = true;
		else
			break;
	}
	// a .ys line keeps the quotes of -eval "a b", drop one pair
	std::string &eval = opt.eval_cmd;
	if (eval.size() >= 2 && eval.front() == '"' && eval.back() == '"')
		eval = eval.substr(1, eval.size() - 2);
	return argidx;
}

void linkOrSync(Design *design, const PassOptions &opt)
{
	if (opt.link) {
		the_sta->epochs.link(design);
		return;
	}
	the_sta->epochs.sync(design);
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

void sourceFile(const std::string &file, const std::string &command, const std::string &tail)
{
	if (file.empty())
		return;
	std::string script = command + " {" + file + "}" + tail;
	if (!tclEval(script))
		log_cmd_error("opensta_net: %s: %s\n", file.c_str(), Tcl_GetStringResult(the_sta->tclInterp()));
}

void evalTcl(const std::string &cmd)
{
	bool ok = tclEval(cmd);
	const char *result = Tcl_GetStringResult(the_sta->tclInterp());
	if (!ok)
		log_cmd_error("OpenSTA tcl error: %s\n", result);
	// the Tcl result of yosys opensta_net -eval
	yosys_get_design()->scratchpad_set_string("result.string", result);
	if (result[0] != 0)
		log("%s\n", result);
}

void OpenstaNetPass::help()
{
	//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
	log("\n");
	log("    opensta_net [options]\n\n");
	log("Boot an embedded OpenSTA instance on its own Tcl interpreter and keep its\n");
	log("STA network in step with the design. OpenSTA network edits (replace_cell,\n");
	log("connect_pin, ...) are refused, edit the design in yosys.\n\n");
	log("    -liberty <file>\n");
	log("        read a liberty file into OpenSTA.\n\n");
	log("    -link\n");
	log("        build the network from scratch. A module used more than once gets one\n");
	log("        set of objects per use. Constraints are cleared.\n\n");
	log("    -unprotect\n");
	log("        remove the barriers (before write_verilog) and end the network.\n\n");
	log("    -sdc <file>\n");
	log("        read an SDC file with read_sdc.\n\n");
	log("    -source <file>\n");
	log("        source a Tcl file in the OpenSTA interpreter.\n\n");
	log("    -eval <tcl>\n");
	log("        evaluate <tcl> in the OpenSTA interpreter and log the result. In a\n");
	log("        Yosys Tcl script, `yosys opensta_net -eval` returns the result.\n\n");
}

void OpenstaNetPass::execute(std::vector<std::string> args, Design *design)
{
	log_header(design, "Executing OPENSTA_NET pass.\n");
	PassOptions opt;
	size_t argidx = parseOptions(args, opt);
	extra_args(args, argidx, design);

	bootSta();
	staNetwork().dropHierNames();
	if (!opt.liberty_file.empty())
		readLiberty(opt.liberty_file);
	linkOrSync(design, opt);
	try {
		sourceFile(opt.sdc_file, "read_sdc", "");
		sourceFile(opt.source_file, "namespace eval ::sta {include_file", " 0 0}");
		if (!opt.eval_cmd.empty())
			evalTcl(opt.eval_cmd);
	} catch (...) {
		// the commands before the error stay
		the_sta->epochs.capture();
		throw;
	}
	if (opt.unprotect)
		the_sta->epochs.unprotect(design);
	else
		the_sta->epochs.capture();
}

OpenstaNetPass opensta_net_pass;

} // namespace opensta_net
