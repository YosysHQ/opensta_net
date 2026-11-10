// the rebuild resolves the refs and applies each command in its mode, clocks and library objects apply by name
// a command that loses or changes its meaning is logged with the reason
#include "opensta_net.h"

#include "sta/Mode.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

int resolveAndApply()
{
	EpochConstraints &constraints = the_sta->constraints;
	the_sta->tracker.resolve(staNetwork().view(), constraints.view_refs);
	std::string cmd_mode = the_sta->cmdMode()->name();
	sta::Scene *cmd_scene = the_sta->cmdScene();
	std::string script = swapInfoScript("");
	int lost = 0;
	for (const SdcCommand &command : constraints.commands) {
		the_sta->setCmdMode(command.mode);
		std::string reason;
		if (applyCommand(command, reason))
			continue;
		lost++;
		log_warning("opensta_net: sync: `%s` lost or changed: %s\n", normalizeCommand(command.log_text).c_str(), reason.c_str());
	}
	// the scene sets its own mode
	the_sta->setCmdScene(cmd_scene);
	the_sta->setCmdMode(cmd_mode);
	swapInfoScript(script);
	// the apply journaled the commands again
	tclEval("::ysta::clear_journal");
	return lost;
}

bool applyCommand(const SdcCommand &command, std::string &reason)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	RenderedCommand rendered = renderCommand(command);
	for (const auto &[handle, name] : rendered.barrier_pin_names)
		the_sta->user_names[handle] = name;
	// OpenSTA drops, narrows or changes a command with an empty getter
	if (rendered.empty_getter) {
		reason = "no object found for " + rendered.missing;
		return false;
	}
	if (rendered.refs_rendered && !rendered.found) {
		reason = "-unprotect removed the barrier that kept its hierarchical pin";
		return false;
	}
	if (Tcl_EvalEx(interp, rendered.script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
		reason = std::string("failed: ") + Tcl_GetStringResult(interp);
		return false;
	}
	if (!rendered.missing.empty()) {
		reason = "applied without " + rendered.missing;
		return false;
	}
	if (rendered.to_drivers) {
		reason = "applied on the drivers of a hierarchical pin whose barrier -unprotect removed";
		return false;
	}
	if (!rendered.moved.empty()) {
		reason = "applied, flatten moved it to the leaf pins inside: " + rendered.moved;
		return false;
	}
	return true;
}

// a getter of barrier instance pins only, opensta ignores their clock attributes
bool onlyBarrierInstancePins(const SdcWord &word)
{
	for (SdcObjectRef *ref : word.refs) {
		if (!ref->origin.on_barrier_instance)
			return false;
	}
	return true;
}

// sets [info script] and returns the last one, sta_warn and sta_error add its file and line to a message
std::string swapInfoScript(const std::string &script)
{
	tclEval("info script");
	std::string last = Tcl_GetStringResult(yosys_get_tcl_interp());
	tclEval("info script " + tclQuoted(script));
	return last;
}

} // namespace opensta_net
