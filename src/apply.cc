// resolve and apply, see resolveAndApply
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
		std::string reason = applyCommand(command);
		if (reason.empty())
			continue;
		lost++;
		std::string normalized = normalizeCommand(command.log_text);
		log_warning("opensta_net: sync: `%s` lost or changed: %s\n", normalized.c_str(), reason.c_str());
	}
	// the scene sets its own mode
	the_sta->setCmdScene(cmd_scene);
	the_sta->setCmdMode(cmd_mode);
	swapInfoScript(script);
	// the apply journaled the commands again
	tclEval("::ysta::clear_journal");
	return lost;
}

std::string applyCommand(const SdcCommand &command)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	RenderedCommand rendered = renderCommand(command);
	// OpenSTA drops, narrows or changes a command with an empty getter
	if (rendered.empty_getter)
		return "no object found for " + rendered.missing;
	if (rendered.refs_rendered && !rendered.found)
		return "-unprotect removed the barrier that kept its hierarchical pin";
	if (Tcl_EvalEx(interp, rendered.script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK)
		return std::string("failed: ") + Tcl_GetStringResult(interp);
	if (!rendered.missing.empty())
		return "applied without " + rendered.missing;
	if (rendered.to_drivers)
		return "applied on the drivers of a hierarchical pin whose barrier -unprotect removed";
	if (!rendered.moved.empty())
		return "applied, flatten moved it to the leaf pins inside: " + rendered.moved;
	return "";
}

// sets [info script], returns the old one
// sta_warn and sta_error add the script file and line to messages
std::string swapInfoScript(const std::string &script)
{
	tclEval("info script");
	std::string last = Tcl_GetStringResult(yosys_get_tcl_interp());
	tclEval("info script " + tclQuoted(script));
	return last;
}

} // namespace opensta_net
