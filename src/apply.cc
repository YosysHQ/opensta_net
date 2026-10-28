// the rebuild resolves the refs and applies each command in its mode, clocks and library objects apply by name
// a command that loses or changes its meaning is logged with the reason
#include "opensta_net.h"

#include "sta/Mode.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

int resolveAndApply()
{
	EpochConstraints &constraints = the_sta->epoch.constraints;
	ysta::YosysNetwork *network = the_sta->yosys_network;
	for (SdcObjectRef &ref : constraints.refs) {
		ref.request.top_port_pin = nullptr;
		if (ref.kind == SdcObjectRef::ObjectKind::Port)
			ref.request.top_port_pin = network->findPin(network->topInstance(), ref.name);
		ref.used_outside_through = false;
		ref.use_objects.clear();
		ref.inside_handled = false;
	}
	the_sta->views.view->resolve(constraints.view_refs);
	std::string cmd_mode = the_sta->cmdMode()->name();
	sta::Scene *cmd_scene = the_sta->cmdScene();
	tclEval("set ::ysta::script [info script]; info script {}");
	int lost = 0;
	for (const SdcCommand &command : constraints.commands) {
		the_sta->setCmdMode(command.mode);
		std::string reason;
		if (applyCommand(command, reason))
			continue;
		lost++;
		log_warning("opensta_net: sync: `%s` lost or changed: %s\n", normalizeCommand(command.log_text).c_str(), reason.c_str());
	}
	the_sta->setCmdMode(cmd_mode);
	the_sta->setCmdScene(cmd_scene);
	tclEval("info script $::ysta::script");
	tclEval("set ::ysta::journal {}");
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
	// an empty getter would widen a path exception
	if (empty_getters > 0) {
		reason = "no object found for " + missing;
		return false;
	}
	if (!refsOf(words).empty() && !foundAny(words)) {
		reason = "-unprotect removed the barrier that kept its hierarchical pin";
		return false;
	}
	int warnings = report->warnings;
	int kept = tclInt("llength $::ysta::kept");
	if (Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
		reason = std::string("failed: ") + Tcl_GetStringResult(interp);
		return false;
	}
	// a warning of a command that the trace keeps again loses nothing
	if (report->warnings != warnings && tclInt("llength $::ysta::kept") == kept) {
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

void appendName(std::string &names, const std::string &name)
{
	if (!names.empty())
		names += " ";
	names += name;
}

// refs that flatten moved inside, a -through or a clock on an input pin keeps its meaning
std::string namesMovedInside(const std::vector<SdcWord> &words)
{
	bool clock = !words.empty() && isClockCommand(words[0].text);
	std::string names;
	for (uint32_t index : refsOf(words)) {
		const SdcObjectRef &ref = the_sta->epoch.constraints.refs[index];
		bool clock_input = clock && ref.facts.hier_dir == Netlist::Dir::Input;
		if (ref.stillAsLeafPins() && ref.used_outside_through && !clock_input)
			appendName(names, ref.name);
	}
	return names;
}

bool anyMovedToDrivers(const std::vector<SdcWord> &words)
{
	for (uint32_t index : refsOf(words)) {
		if (the_sta->epoch.constraints.refs[index].facts.moved_to_drivers)
			return true;
	}
	return false;
}

bool foundAny(const std::vector<SdcWord> &words)
{
	for (uint32_t index : refsOf(words)) {
		if (!the_sta->epoch.constraints.refs[index].objects.empty())
			return true;
	}
	return false;
}

// a getter of image pins only, opensta ignores their clock attributes
bool onlyImagePins(const SdcWord &word)
{
	for (uint32_t index : word.refs) {
		if (!the_sta->epoch.constraints.refs[index].facts.is_image_pin)
			return false;
	}
	return true;
}

std::string missingNames(const std::vector<SdcWord> &words)
{
	std::string names;
	for (uint32_t index : refsOf(words)) {
		const SdcObjectRef &ref = the_sta->epoch.constraints.refs[index];
		if (ref.objects.empty() && !ref.facts.is_image_pin)
			appendName(names, ref.name);
	}
	return names;
}

int tclInt(const std::string &script)
{
	tclEval(script);
	return atoi(Tcl_GetStringResult(yosys_get_tcl_interp()));
}

} // namespace opensta_net
