#include "opensta_net.h"

#include <charconv>
#include <fstream>

#include "sta/Error.hh"
#include "sta/MinMax.hh"
#include "sta/Mode.hh"
#include "sta/Parasitics.hh"
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
	yosys_network->setBeforeCheck(openEpoch);
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

// Line continuations and whitespace runs collapsed
std::string normalizeCommand(const std::string &cmd)
{
	std::string out;
	bool space = false;
	for (size_t i = 0; i < cmd.size(); i++) {
		if (cmd[i] == '\\' && i + 1 < cmd.size() && cmd[i + 1] == '\n') {
			space = true;
			i++;
		} else if (isspace(static_cast<unsigned char>(cmd[i]))) {
			space = true;
		} else {
			if (space && !out.empty())
				out += ' ';
			space = false;
			out += cmd[i];
		}
	}
	return out;
}

std::vector<std::string> sdcCommands(const std::string &text)
{
	std::vector<std::string> commands;
	std::stringstream in(text);
	std::string line, cmd;
	while (std::getline(in, line)) {
		cmd += line + "\n";
		if ((!line.empty() && line.back() == '\\') || !Tcl_CommandComplete(cmd.c_str()))
			continue;
		std::string normal = normalizeCommand(cmd);
		if (!normal.empty() && normal[0] != '#')
			commands.push_back(cmd);
		cmd.clear();
	}
	// Incomplete: its evaluation reports the error
	if (!normalizeCommand(cmd).empty())
		commands.push_back(cmd);
	return commands;
}

void countScope(const Netlist::Instance *scope, size_t &insts, size_t &nets, size_t &pins)
{
	nets += the_sta->view->nets(scope).size();
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
	size_t pins = the_sta->view->top()->pins.size();
	countScope(the_sta->view->top(), insts, nets, pins);
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
	if (the_sta->view->built())
		the_sta->retired_views.push_back(std::exchange(the_sta->view, the_sta->view->retire()));
}

bool linkView(Design *design, std::string &error)
{
	dropView();
	auto t0 = std::chrono::steady_clock::now();
	ysta::YosysNetwork *network = the_sta->yosys_network;
	network->attach(the_sta->view.get());
	try {
		the_sta->view->build(design, network);
		network->link();
	} catch (const sta::Exception &exception) {
		network->unlink();
		the_sta->view->reset();
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
	the_sta->held.reset();
	the_sta->yosys_network->setLocked(false);
	if (!linkView(design, reason))
		log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
}

bool writeModeSdc(const std::string &mode, std::string &text)
{
	std::string file = make_temp_file(get_base_tmpdir() + "/opensta_net_XXXXXX.sdc");
	bool ok = tclEval("write_sdc -no_timestamp -mode {" + mode + "} {" + file + "}");
	if (ok) {
		std::stringstream stream;
		stream << std::ifstream(file).rdbuf();
		text = stream.str();
	} else {
		log_warning("opensta_net: write_sdc failed: %s\n", Tcl_GetStringResult(yosys_get_tcl_interp()));
	}
	remove(file.c_str());
	return ok;
}

bool staleConstraints(std::vector<ModeSdc> &result)
{
	bool ok = true;
	the_sta->yosys_network->setReadStale(true);
	for (const sta::Mode *mode : the_sta->modes()) {
		ModeSdc &sdc = result.emplace_back();
		sdc.mode = mode->name();
		ok = ok && writeModeSdc(sdc.mode, sdc.text);
	}
	the_sta->yosys_network->setReadStale(false);
	return ok;
}

int applyCommands(const std::vector<std::string> &commands)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	YosysStaReport *report = static_cast<YosysStaReport *>(the_sta->report());
	int lost = 0;
	for (const std::string &cmd : commands) {
		int warnings = report->warnings;
		bool failed = Tcl_EvalEx(interp, cmd.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK;
		if (failed)
			log_warning("opensta_net: sync: `%s` failed: %s\n", normalizeCommand(cmd).c_str(), Tcl_GetStringResult(interp));
		lost += failed || report->warnings != warnings;
	}
	return lost;
}

int reapplyConstraints(const std::vector<ModeSdc> &before)
{
	std::string cmd_mode = the_sta->cmdMode()->name();
	sta::Scene *cmd_scene = the_sta->cmdScene();
	tclEval("set ::ysta::script [info script]; info script {}");
	int lost = 0;
	for (const ModeSdc &mode : before) {
		the_sta->setCmdMode(mode.mode);
		lost += applyCommands(sdcCommands(mode.text));
	}
	the_sta->setCmdMode(cmd_mode);
	the_sta->setCmdScene(cmd_scene);
	tclEval("info script $::ysta::script");
	return lost;
}

void warnDroppedParasitics()
{
	for (const sta::Scene *scene : the_sta->scenes()) {
		for (const sta::MinMax *min_max : sta::MinMax::range()) {
			sta::Parasitics *parasitics = scene->parasitics(min_max);
			if (parasitics != nullptr && parasitics->haveParasitics()) {
				log_warning("opensta_net: the rebuild drops the loaded parasitics, read them again.\n");
				return;
			}
		}
	}
}

bool canBuild(Design *design)
{
	std::string reason;
	if (NetView::buildable(design, reason)) {
		the_sta->unbuildable.clear();
		return true;
	}
	if (reason == the_sta->unbuildable)
		return false;
	the_sta->unbuildable = reason;
	log_warning("opensta_net: cannot rebuild the STA network: %s. It stays unlinked until a sync succeeds.\n",
	            reason.c_str());
	return false;
}

bool needsSync()
{
	if (the_sta->held)
		return true;
	return the_sta->yosys_network->linked() && !the_sta->view->valid();
}

void rebuild(Design *design)
{
	std::vector<ModeSdc> before;
	if (the_sta->held) {
		before = std::move(*the_sta->held);
		the_sta->held.reset();
	} else if (!staleConstraints(before)) {
		log_warning("opensta_net: cannot write the constraints, the STA network stays stale.\n");
		return;
	} else {
		warnDroppedParasitics();
	}
	std::string error;
	if (!linkView(design, error)) {
		the_sta->held = std::move(before);
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync succeeds.\n",
		            error.c_str());
		return;
	}
	int lost = reapplyConstraints(before);
	log("opensta_net: synced after design change, %d constraint(s) lost.\n", lost);
}

void syncView(Design *design)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	the_sta->view->invalidateCheck();
	if (the_sta->syncing || (!network->linked() && !the_sta->held))
		return;
	if (!needsSync()) {
		network->setLocked(false);
		return;
	}
	if (!canBuild(design))
		return;
	the_sta->syncing = true;
	network->setLocked(false);
	rebuild(design);
	the_sta->syncing = false;
}

void closeEpoch()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (the_sta->syncing || !network->linked() || network->locked())
		return;
	network->setLocked(true);
}

void openEpoch()
{
	bool closed = the_sta->yosys_network->locked() || the_sta->held;
	if (closed && yosys_design != nullptr)
		syncView(yosys_design);
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
	if (pass_register.size() != the_sta->wrapped)
		wrapPasses();
}

int tclAfterYosys(ClientData, Tcl_Interp *, int, Tcl_Obj *const[])
{
	the_sta->view->invalidateCheck();
	if (the_sta->autosync && yosys_design != nullptr)
		syncView(yosys_design);
	return TCL_OK;
}

void *swigPointer(std::string_view handle, std::string_view type)
{
	const size_t digits = 2 * sizeof(void *);
	if (handle.size() != 1 + digits + 3 + type.size() || handle[0] != '_' ||
	    handle.substr(1 + digits, 3) != "_p_" || handle.substr(4 + digits) != type)
		return nullptr;
	unsigned char bytes[sizeof(void *)];
	for (size_t i = 0; i < sizeof(void *); i++) {
		const char *first = handle.data() + 1 + 2 * i;
		std::from_chars_result parsed = std::from_chars(first, first + 2, bytes[i], 16);
		if (parsed.ec != std::errc() || parsed.ptr != first + 2)
			return nullptr;
	}
	void *pointer;
	memcpy(&pointer, bytes, sizeof(pointer));
	return pointer;
}

void recordHandle(std::string_view handle)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (void *inst = swigPointer(handle, "Instance"))
		network->recordInstance(static_cast<const sta::Instance *>(inst));
	else if (void *pin = swigPointer(handle, "Pin"))
		network->recordPin(static_cast<const sta::Pin *>(pin));
	else if (void *net = swigPointer(handle, "Net"))
		network->recordNet(static_cast<const Netlist::Net *>(net));
}

int tclRecordResult(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	int code, count;
	Tcl_Obj **items;
	if (objc < 5 || !the_sta->yosys_network->recording() || Tcl_GetIntFromObj(interp, objv[2], &code) != TCL_OK ||
	    code != TCL_OK || Tcl_ListObjGetElements(interp, objv[3], &count, &items) != TCL_OK)
		return TCL_OK;
	for (int i = 0; i < count; i++)
		recordHandle(Tcl_GetString(items[i]));
	return TCL_OK;
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

void wrapClockSense()
{
	std::string wrap = "rename ::sta::set_clock_sense_cmd ::sta::ysta_set_clock_sense_cmd; "
	                   "proc ::sta::set_clock_sense_cmd {pins args} "
	                   "{ if {[llength $pins]} { ::sta::ysta_set_clock_sense_cmd $pins {*}$args } }";
	if (!tclEval(wrap))
		log_error("OpenSTA: cannot wrap set_clock_sense_cmd: %s\n", Tcl_GetStringResult(yosys_get_tcl_interp()));
}

void traceExecution(const char *command, const char *op, const char *handler)
{
	if (!tclEval(std::string("trace add execution ") + command + " " + op + " " + handler))
		log_error("OpenSTA: cannot trace %s: %s\n", command, Tcl_GetStringResult(yosys_get_tcl_interp()));
}

void addTraces()
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	Tcl_CreateObjCommand(interp, "ysta::before_edit", tclBeforeEdit, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::after_yosys", tclAfterYosys, nullptr, nullptr);
	Tcl_CreateObjCommand(interp, "ysta::record_result", tclRecordResult, nullptr, nullptr);
	for (const char *editor : {"yosys", "rtlil::set_attr", "rtlil::set_param"})
		traceExecution(editor, "leave", "ysta::after_yosys");
	for (const char *editor : {"rtlil::set_attr", "rtlil::set_param"})
		traceExecution(editor, "enter", "ysta::before_edit");
	for (const char *getter : {"get_cells", "get_pins", "get_nets", "get_fanin", "get_fanout", "all_registers"})
		traceExecution(getter, "leave", "ysta::record_result");
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
	the_sta->wrapped = pass_register.size();
}

void bootSta()
{
	if (the_sta != nullptr)
		return;
	makeSta();
	wrapClockSense();
	addTraces();
	wrapPasses();
	log("OpenSTA instance created on the Yosys Tcl interpreter.\n");
}

size_t parseOptions(const std::vector<std::string> &args, Options &opt)
{
	const std::map<std::string, std::string *> values = {{"-eval", &opt.eval_cmd}, {"-liberty", &opt.liberty_file},
	                                                     {"-source", &opt.source_file}, {"-sdc", &opt.sdc_file}};
	const std::map<std::string, bool *> flags = {{"-link", &opt.link}, {"-sync", &opt.sync},
	                                             {"-autosync", &opt.autosync}, {"-keep_hierarchy", &opt.keep_hierarchy}};
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

void linkOrSync(Design *design, const Options &opt)
{
	if (opt.link) {
		linkDesign(design);
		return;
	}
	// Passes may have run since the last call
	the_sta->view->invalidateCheck();
	if (!opt.sync && needsSync())
		log_warning("opensta_net: design changed since link, rebuilding the STA network "
		            "and re-applying the constraints.\n");
	syncView(design);
}

void sourceSdc(Design *design, const Options &opt)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (!opt.keep_hierarchy) {
		checkSourced(sourceFile(opt.sdc_file, true), opt.sdc_file);
		return;
	}
	network->beginRecording();
	bool ok = sourceFile(opt.sdc_file, true);
	std::set<Netlist::Instance *> scopes = network->endRecording();
	checkSourced(ok, opt.sdc_file);
	the_sta->view->invalidateCheck();
	if (!the_sta->view->valid()) {
		log_warning("opensta_net: design changed while the constraints were read, nothing marked.\n");
		syncView(design);
		return;
	}
	markKeepHierarchy(scopes);
}

void markKeepHierarchy(const std::set<Netlist::Instance *> &scopes)
{
	pool<RTLIL::Module *> modules;
	for (Netlist::Instance *scope : scopes)
		modules.insert(the_sta->view->module(scope));
	for (RTLIL::Module *module : modules) {
		module->set_bool_attribute(ID::keep_hierarchy);
		log("opensta_net: keep_hierarchy on %s\n", module);
	}
	log("opensta_net: marked %zu modules keep_hierarchy.\n", modules.size());
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
	log("        re-apply the current constraints. Every call does this implicitly.\n");
	log("\n");
	log("    -autosync\n");
	log("        also sync after every `yosys`, `rtlil::set_attr` or `rtlil::set_param`\n");
	log("        command run from Tcl.\n");
	log("\n");
	log("    -sdc <file>\n");
	log("        source an SDC file in the shared interpreter.\n");
	log("\n");
	log("    -keep_hierarchy\n");
	log("        with -sdc: mark modules referenced by the constraints (and their\n");
	log("        ancestors) with the keep_hierarchy attribute.\n");
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
	if (opt.autosync)
		the_sta->autosync = true;
	if (!opt.liberty_file.empty())
		readLiberty(opt.liberty_file);
	linkOrSync(design, opt);
	if (!opt.sdc_file.empty())
		sourceSdc(design, opt);
	if (!opt.source_file.empty())
		checkSourced(sourceFile(opt.source_file, false), opt.source_file);
	if (!opt.eval_cmd.empty())
		evalTcl(opt.eval_cmd);
}

OpenstaNetPass opensta_net_pass;

} // namespace opensta_net
