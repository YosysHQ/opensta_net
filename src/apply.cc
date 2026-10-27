// the rebuild resolves the refs and applies each command in its mode, clocks and library objects apply by name
// a command that loses or changes its meaning is logged with the reason
#include "opensta_net.h"

#include "sta/Mode.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

int applyConstraints()
{
	Constraints &constraints = the_sta->constraints;
	ysta::YosysNetwork *network = the_sta->yosys_network;
	for (Ref &ref : constraints.refs) {
		ref.request.top_port_pin = ref.kind == Ref::ObjectKind::Port ? network->findPin(network->topInstance(), ref.name) : nullptr;
		ref.plain = false;
		ref.use.clear();
	}
	the_sta->view->resolve(constraints.view_refs);
	std::string cmd_mode = the_sta->cmdMode()->name();
	sta::Scene *cmd_scene = the_sta->cmdScene();
	tclEval("set ::ysta::script [info script]; info script {}");
	int lost = 0;
	for (const Command &command : the_sta->constraints.commands) {
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

bool applyCommand(const Command &command, std::string &reason)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	YosysStaReport *report = static_cast<YosysStaReport *>(the_sta->report());
	int empty_getters = 0;
	applyClockImages(command.words);
	std::string script = command.words.empty() ? command.text : render(command.words, empty_getters, false);
	std::string missing = missingNames(command.words);
	std::string moved = movedInside(command.words);
	// an empty getter would widen a path exception
	if (empty_getters > 0) {
		reason = "no object found for " + missing;
		return false;
	}
	if (!refsOf(command.words).empty() && !foundAny(command.words)) {
		reason = "-unprotect removed the barrier that kept its hierarchical pin";
		return false;
	}
	int warnings = report->warnings;
	if (Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
		reason = std::string("failed: ") + Tcl_GetStringResult(interp);
		return false;
	}
	if (report->warnings != warnings) {
		reason = "applied with a warning";
		return false;
	}
	if (!missing.empty()) {
		reason = "applied without " + missing;
		return false;
	}
	return keptMeaning(command.words, moved, reason);
}

bool keptMeaning(const std::vector<Word> &words, const std::string &moved, std::string &reason)
{
	if (anyUnprotected(words)) {
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
std::string movedInside(const std::vector<Word> &words)
{
	bool clock = !words.empty() && isClockCommand(words[0].text);
	std::string names;
	for (uint32_t index : refsOf(words)) {
		const Ref &ref = the_sta->constraints.refs[index];
		if (ref.result.found_as_leaf_pins && ref.plain && !(clock && ref.facts.hier_dir == Netlist::Dir::Input))
			names += (names.empty() ? "" : " ") + ref.name;
	}
	return names;
}

bool anyUnprotected(const std::vector<Word> &words)
{
	for (uint32_t index : refsOf(words)) {
		if (the_sta->constraints.refs[index].facts.moved_to_drivers)
			return true;
	}
	return false;
}

bool foundAny(const std::vector<Word> &words)
{
	for (uint32_t index : refsOf(words)) {
		if (!the_sta->constraints.refs[index].objects.empty())
			return true;
	}
	return false;
}

// a getter of image pins only, opensta ignores their clock attributes
bool onlyImages(const Word &word)
{
	for (uint32_t index : word.refs) {
		if (!the_sta->constraints.refs[index].facts.is_image_pin)
			return false;
	}
	return true;
}

std::string missingNames(const std::vector<Word> &words)
{
	std::string names;
	for (uint32_t index : refsOf(words)) {
		const Ref &ref = the_sta->constraints.refs[index];
		if (ref.objects.empty() && !ref.facts.is_image_pin)
			names += (names.empty() ? "" : " ") + ref.name;
	}
	return names;
}

int tclInt(const std::string &script)
{
	tclEval(script);
	return atoi(Tcl_GetStringResult(yosys_get_tcl_interp()));
}

} // namespace opensta_net
