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

size_t YosysStaReport::printConsole(const char *buffer, size_t length)
{
	std::string_view text(buffer, length);
	LogMessage msg(LogSeverity::Info, {}, "", "%s", text);
	msg.cached_msg = std::string(text);
	logger().for_each_sink(NonConsoleTee{msg});
	return sta::ReportTcl::printConsole(buffer, length);
}

void YosysSta::makeReport() { report_ = new YosysStaReport; }

void YosysSta::makeNetwork() { network_ = new ysta::YosysNetwork(queryHook); }

bool tclEval(const std::string &script) { return Tcl_Eval(yosys_get_tcl_interp(), script.c_str()) == TCL_OK; }

bool queryHook() { return the_sta->epochs.syncForQuery(yosys_get_design()); }

int tclBeforeEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[])
{
	the_sta->epochs.end();
	return TCL_OK;
}

int tclAfterEdit(ClientData, Tcl_Interp *, int, Tcl_Obj *const[])
{
	if (the_sta->autosync && yosys_get_design() != nullptr)
		the_sta->epochs.sync(yosys_get_design());
	return TCL_OK;
}

bool topHasScopes(Design *design)
{
	RTLIL::Module *top = nullptr;
	for (RTLIL::Module *module : design->modules()) {
		if (module->get_bool_attribute(ID::top))
			top = module;
	}
	if (top == nullptr)
		return true; // not known, keep the sync
	for (RTLIL::Cell *cell : top->cells()) {
		RTLIL::Module *type = design->module(cell->type);
		if (type != nullptr && !type->get_blackbox_attribute())
			return true;
	}
	return false;
}

void beforePass(Pass *pass, Design *design)
{
	// flatten keeps no name of a scope with a private name, a checkpoint with the names of now keeps such a scope
	// not for the map design of a techmap, a flat design has no scope to keep
	bool is_flatten = pass->pass_name == "flatten" && design == yosys_get_design();
	if (is_flatten && the_sta->track_barriers && topHasScopes(design))
		the_sta->epochs.sync(design);
	if (pass->pass_name != "opensta_net")
		the_sta->epochs.end();
}

void makeSta()
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
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
	Tcl_Interp *interp = yosys_get_tcl_interp();
	Tcl_CreateObjCommand(interp, "ysta::before_edit", tclBeforeEdit, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::after_edit", tclAfterEdit, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::resolve", tclResolve, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::moved_ends", tclMovedEnds, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::moved_arc", tclMovedArc, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::missing", tclMissing, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::loss", tclLoss, nullptr, nullptr);
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
	Pass::pre_call_hooks.push_back(beforePass);
	log("OpenSTA instance created on the Yosys Tcl interpreter.\n");
}

size_t parseOptions(const std::vector<std::string> &args, PassOptions &opt)
{
	const std::map<std::string, std::string *> values = {
		{"-eval", &opt.eval_cmd}, {"-liberty", &opt.liberty_file},
		{"-source", &opt.source_file}, {"-sdc", &opt.sdc_file}
	};
	const std::map<std::string, bool *> flags = {
		{"-link", &opt.link}, {"-sync", &opt.sync}, {"-autosync", &opt.autosync},
		{"-protect", &opt.protect}, {"-unprotect", &opt.unprotect}
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
	if (!opt.sync && the_sta->epochs.stale())
		log_warning("opensta_net: design changed since link, rebuilding the STA network "
		            "and re-applying the constraints.\n");
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
	if (!file.empty() && !tclEval(command + " {" + file + "}" + tail))
		log_cmd_error("opensta_net: %s: %s\n", file.c_str(), Tcl_GetStringResult(yosys_get_tcl_interp()));
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

void OpenstaNetPass::help()
{
	//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
	log("\n");
	log("    opensta_net [options]\n\n");
	log("Boot an embedded OpenSTA instance on the Yosys Tcl interpreter and keep its\n");
	log("netlist view in step with the design. OpenSTA network edits (replace_cell,\n");
	log("connect_pin, ...) are refused, edit the design in yosys.\n\n");
	log("    -liberty <file>\n");
	log("        read a liberty file into OpenSTA.\n\n");
	log("    -link\n");
	log("        build the view from scratch. A module used more than once gets one\n");
	log("        set of objects per use. Constraints are cleared.\n\n");
	log("    -sync\n");
	log("        if the design changed since the view was built, rebuild it and\n");
	log("        apply the constraints again. Every call and every OpenSTA query\n");
	log("        does this implicitly.\n\n");
	log("    -autosync\n");
	log("        also sync after each `yosys`, `rtlil::set_attr` or `rtlil::set_param`\n");
	log("        command run from Tcl.\n\n");
	log("    -protect\n");
	log("        protect the objects again after -unprotect, the constraints stay.\n\n");
	log("    -unprotect\n");
	log("        remove the barriers and keeps (before write_verilog) and put no new\n");
	log("        ones. A later pass can remove a constrained object, each lost command\n");
	log("        is reported. A pass can also merge an equal cell into a constrained\n");
	log("        one, the constraint is then on both.\n\n");
	log("    -sdc <file>\n");
	log("        source an SDC file in the shared interpreter.\n\n");
	log("    -source <file>\n");
	log("        source a Tcl file in the shared interpreter.\n\n");
	log("    -eval <tcl>\n");
	log("        evaluate <tcl> in the shared interpreter and log the result.\n\n");
}

void OpenstaNetPass::execute(std::vector<std::string> args, Design *design)
{
	log_header(design, "Executing OPENSTA_NET pass.\n");
	PassOptions opt;
	size_t argidx = parseOptions(args, opt);
	extra_args(args, argidx, design);

	bootSta();
	the_sta->autosync |= opt.autosync;
	if (!opt.liberty_file.empty())
		readLiberty(opt.liberty_file);
	linkOrSync(design, opt);
	sourceFile(opt.sdc_file, "read_sdc", "");
	sourceFile(opt.source_file, "namespace eval ::sta {include_file", " 0 0}");
	if (!opt.eval_cmd.empty())
		evalTcl(opt.eval_cmd);
	the_sta->track_barriers |= opt.protect;
	if (opt.unprotect)
		the_sta->epochs.unprotect(design);
}

OpenstaNetPass opensta_net_pass;

} // namespace opensta_net
