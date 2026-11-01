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
	the_sta->tracker.resolve(the_sta->view, constraints.view_refs);
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
	std::string missing = missingNames(words);
	std::string moved = namesMovedInside(command.words);
	// OpenSTA drops, narrows or changes a command with an empty getter
	if (empty_getters > 0) {
		reason = "no object found for " + missing;
		return false;
	}
	if (!refsOf(words).empty() && !foundAny(words)) {
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
	if (!missing.empty()) {
		reason = "applied without " + missing;
		return false;
	}
	return keptMeaning(command.words, moved, reason);
}

bool keptMeaning(const std::vector<SdcWord> &words, const std::string &moved, std::string &reason)
{
	if (anyMovedToDrivers(words)) {
		reason = "applied on the drivers of a hierarchical pin whose barrier -unprotect removed";
		return false;
	}
	if (!moved.empty()) {
		reason = "applied, flatten moved it to the leaf pins inside: " + moved;
		return false;
	}
	return true;
}

// refs that flatten moved inside, a -through or a clock on an input pin keeps its meaning
std::string namesMovedInside(const std::vector<SdcWord> &words)
{
	bool clock = !words.empty() && isClockCommand(words[0].text);
	std::string names;
	for (SdcObjectRef *ref : refsOf(words)) {
		bool clock_input = clock && ref->origin.hier_pin_dir == Netlist::PortDir::Input;
		if (ref->stillAsLeafPins() && ref->used_outside_through && !clock_input)
			names += (names.empty() ? "" : " ") + ref->name;
	}
	return names;
}

bool anyMovedToDrivers(const std::vector<SdcWord> &words)
{
	for (SdcObjectRef *ref : refsOf(words)) {
		if (ref->origin.anchored_on_drivers)
			return true;
	}
	return false;
}

bool foundAny(const std::vector<SdcWord> &words)
{
	for (SdcObjectRef *ref : refsOf(words)) {
		if (!ref->objects().empty())
			return true;
	}
	return false;
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

std::string missingNames(const std::vector<SdcWord> &words)
{
	std::string names;
	for (SdcObjectRef *ref : refsOf(words)) {
		if (ref->objects().empty() && !ref->origin.on_barrier_instance)
			names += (names.empty() ? "" : " ") + ref->name;
	}
	return names;
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
