// the rebuild resolves the refs and applies each command in its mode, clocks and library objects apply by name
// a command that loses or changes its meaning is logged with the reason
#include "opensta_net.h"

#include "sta/Mode.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

int resolveAndApply()
{
	EpochConstraints &constraints = the_sta->constraints;
	for (SdcObjectRef &ref : constraints.refs) {
		ref.used_outside_through = false;
		ref.use_objects.clear();
		ref.inside_handled = false;
	}
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
	YosysStaReport *report = static_cast<YosysStaReport *>(the_sta->report());
	int empty_getters = 0;
	std::vector<SdcWord> words = command.words;
	rewriteWords(words);
	std::string script = command.text;
	if (!words.empty())
		script = renderWords(words, empty_getters, ThroughContext::OutsideThrough);
	RefLosses rendered = refLosses(words);
	RefLosses original = refLosses(command.words);
	// OpenSTA drops, narrows or changes a command with an empty getter
	if (empty_getters > 0) {
		reason = "no object found for " + rendered.missing;
		return false;
	}
	if (!refsOf(words).empty() && !rendered.found) {
		reason = "-unprotect removed the barrier that kept its hierarchical pin";
		return false;
	}
	int warnings = report->warnings;
	int kept = tclInt("::ysta::kept_count");
	if (Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
		reason = std::string("failed: ") + Tcl_GetStringResult(interp);
		return false;
	}
	// a warning of a command that the trace keeps again loses nothing
	if (report->warnings != warnings && tclInt("::ysta::kept_count") == kept) {
		reason = "applied with a warning";
		return false;
	}
	if (!rendered.missing.empty()) {
		reason = "applied without " + rendered.missing;
		return false;
	}
	if (original.to_drivers) {
		reason = "applied on the drivers of a hierarchical pin whose barrier -unprotect removed";
		return false;
	}
	if (!original.moved.empty()) {
		reason = "applied, flatten moved it to the leaf pins inside: " + original.moved;
		return false;
	}
	return true;
}

// a -through or a clock on an input pin that flatten moved inside keeps its meaning
RefLosses refLosses(const std::vector<SdcWord> &words)
{
	bool clock = !words.empty() && isClockCommand(words[0].text);
	RefLosses losses;
	for (SdcObjectRef *ref : refsOf(words)) {
		bool none = ref->objects().empty();
		bool clock_input = clock && ref->origin.hier_pin_dir == Netlist::PortDir::Input;
		losses.found |= !none;
		losses.to_drivers |= ref->origin.anchored_on_drivers;
		if (none && !ref->origin.on_barrier_instance)
			losses.missing += (losses.missing.empty() ? "" : " ") + ref->name;
		if (ref->stillAsLeafPins() && ref->used_outside_through && !clock_input)
			losses.moved += (losses.moved.empty() ? "" : " ") + ref->name;
	}
	return losses;
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

int tclInt(const std::string &script)
{
	tclEval(script);
	return atoi(Tcl_GetStringResult(yosys_get_tcl_interp()));
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
