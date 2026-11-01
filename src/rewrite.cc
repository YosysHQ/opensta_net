// the words on the objects that passes moved, then the clock names, in the mode of the command
// (a removed hierarchical pin goes on the leaf pins that opensta takes for it)
#include "opensta_net.h"

#include "sta/Mode.hh"
#include "sta/Sdc.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

void rewriteWords(std::vector<SdcWord> &words)
{
	if (words.empty())
		return;
	remapArcPorts(words);
	splitEndpoints(words);
	disableWireEdges(words);
	hierPinLoads(words);
	invertClock(words);
	applyClockPins(words);
	quoteClocks(words);
}

// abc maps a gate to cells: the arc takes their ports, only on the cells of the -from pins
void remapArcPorts(std::vector<SdcWord> &words)
{
	for (size_t i = 0; i + 1 < words.size(); i++) {
		if (words[i].text != "-from" && words[i].text != "-to")
			continue;
		std::string &text = words[i + 1].text;
		std::string name = text;
		if (text.size() > 2 && text.front() == '{')
			name = text.substr(1, text.size() - 2);
		ArcEnd end = ArcEnd::To;
		if (words[i].text == "-from")
			end = ArcEnd::From;
		for (SdcObjectRef *ref : refsOf(words))
			remapPort(*ref, name, end, text);
	}
}

void remapPort(SdcObjectRef &ref, const std::string &name, ArcEnd end, std::string &text)
{
	for (const auto &[old_port, old_bit, pin] : ref.result.port_renames) {
		if (old_port != name)
			continue;
		text = "{" + pin->port().name + "}";
		if (end == ArcEnd::From)
			ref.use_objects.push_back(staNetwork().handle(pin->inst));
	}
}

// a -to behind new logic (a mux before D): -through the old signal, -to the exact pins
void splitEndpoints(std::vector<SdcWord> &words)
{
	static const pool<std::string> commands = {"set_false_path", "set_multicycle_path", "set_max_delay", "set_min_delay", "set_data_check"};
	if (!commands.count(words[0].text))
		return;
	bool check = words[0].text == "set_data_check";
	for (size_t i = 1; i + 1 < words.size(); i++) {
		bool to = words[i].text == "-to" || words[i].text == "-rise_to" || words[i].text == "-fall_to";
		bool from = words[i].text == "-from" || words[i].text == "-rise_from" || words[i].text == "-fall_from";
		if (!anyBehindNewLogic(words[i + 1]))
			continue;
		if (from)
			exactStartPins(words, i);
		if (!to)
			continue;
		SdcWord through = words[i + 1];
		words[i + 1].pin_choice = PinChoice::ExactPins;
		if (check)
			continue;
		words.insert(words.begin() + i, {SdcWord{"-through"}, through});
		i += 2;
	}
}

// a -from behind new logic (an inverter before C): the exact pin with the edge of the old signal
void exactStartPins(std::vector<SdcWord> &words, size_t at)
{
	SdcWord found = words[at + 1];
	words[at + 1].pin_choice = PinChoice::ExactPins;
	if (words[at].text == "-from")
		return;
	SdcWord flag;
	flag.kind = WordKind::Substitution;
	flag.nested_words = {SdcWord{"::ysta::start_edge"}, SdcWord{words[at].text}, found};
	words[at] = flag;
}

// a removed hierarchical pin or cell: the wire edges into the loads beyond its barriers
void disableWireEdges(std::vector<SdcWord> &words)
{
	if (words[0].text != "set_disable_timing")
		return;
	for (SdcWord &word : words) {
		if (word.refs.size() != 1)
			continue;
		SdcObjectRef &ref = *word.refs[0];
		if (!ref.stillAsLeafPins() && !ref.removedButLoadsKept())
			continue;
		std::string handles;
		for (const Netlist::Pin *pin : ref.result.loads_beyond)
			handles += " " + swigHandle(staNetwork().handle(pin));
		word.refs.clear();
		word.kind = WordKind::Plain;
		word.text = "[::ysta::wire_edges [list" + handles + "]]";
		ref.inside_handled = true;
	}
}

// a removed hierarchical pin in a case value or a -through
void hierPinLoads(std::vector<SdcWord> &words)
{
	bool all = words[0].text == "set_case_analysis";
	for (size_t i = 1; i < words.size(); i++) {
		if (!all && !isThroughFlag(words[i - 1].text))
			continue;
		for (SdcObjectRef *ref : words[i].refs) {
			if (ref->stillAsLeafPins() && ref->origin.hier_pin_dir == Netlist::PortDir::Output)
				ref->use_objects = handlesOf(ref->result.loads_beyond);
			ref->inside_handled = true;
		}
	}
}

// a generated clock behind a new inverter after Q goes on Q inverted, opensta shifts an -edge_shift inverse by a period
void invertClock(std::vector<SdcWord> &words)
{
	if (words[0].text != "create_generated_clock" || !anyBehindNewLogic(words.back()) || hasFlag(words, "-edge_shift"))
		return;
	std::string found, exact;
	for (SdcObjectRef *ref : words.back().refs) {
		found += refHandles(*ref, ThroughContext::OutsideThrough, PinChoice::FoundPins);
		exact += refHandles(*ref, ThroughContext::OutsideThrough, PinChoice::ExactPins);
	}
	if (tclInt("::ysta::behind_inverter [list" + found + "] [list" + exact + "]") != 1)
		return;
	words.back().pin_choice = PinChoice::ExactPins;
	bool edges = false;
	size_t invert = 0;
	for (size_t i = 1; i + 1 < words.size(); i++) {
		if (words[i].text == "-invert")
			invert = i;
		if (words[i].text == "-edges") {
			words[i + 1].text = rotateEdges(words[i + 1].text);
			edges = true;
		}
	}
	if (invert != 0)
		words.erase(words.begin() + invert);
	else if (!edges)
		words.insert(words.end() - 1, SdcWord{"-invert"});
}

// the edges of the inverse clock, {a b c} as {b c c+b-a}
std::string rotateEdges(const std::string &text)
{
	std::vector<std::string> parts = split_tokens(text.substr(1, text.size() - 2), " ");
	if (parts.size() != 3)
		return text;
	int next = atoi(parts[2].c_str()) + atoi(parts[1].c_str()) - atoi(parts[0].c_str());
	return "{" + parts[1] + " " + parts[2] + " " + std::to_string(next) + "}";
}

bool anyBehindNewLogic(const SdcWord &word)
{
	for (SdcObjectRef *ref : word.refs) {
		if (ref->isBehindNewLogic())
			return true;
	}
	return false;
}

bool hasFlag(const std::vector<SdcWord> &words, const std::string &flag)
{
	for (const SdcWord &word : words) {
		if (word.text == flag)
			return true;
	}
	return false;
}

void applyClockPins(const std::vector<SdcWord> &words)
{
	if (isClockCommand(words[0].text))
		setClockPins(words, ClockUse::Clock);
	else if (words[0].text == "set_clock_latency" && hasFlag(words, "-source"))
		setClockPins(words, ClockUse::SourceLatency);
	else if (words[0].text == "set_clock_latency")
		setClockPins(words, ClockUse::NetworkLatency);
}

void setClockPins(const std::vector<SdcWord> &words, ClockUse use)
{
	for (size_t i = 0; i < words.size(); i++) {
		ClockUse word_use = use;
		if (use == ClockUse::Clock && i > 0 && words[i - 1].text == "-source")
			word_use = ClockUse::GeneratedClockSource;
		for (SdcObjectRef *ref : words[i].refs)
			pickClockPins(*ref, word_use);
		setClockPins(words[i].nested_words, use);
	}
}

// where a clock use of a hierarchical pin goes when flatten removed the pin
void pickClockPins(SdcObjectRef &ref, ClockUse use)
{
	using PortDir = Netlist::PortDir;
	bool output_side = ref.origin.hier_pin_dir == PortDir::Output || use == ClockUse::GeneratedClockSource;
	if (use == ClockUse::Clock && ref.origin.hier_pin_dir == PortDir::Input)
		ref.use_objects = loadsInside(ref);
	else if (!ref.stillAsLeafPins())
		return;
	else if (use == ClockUse::NetworkLatency && !ref.result.loads_beyond.empty())
		ref.use_objects = handlesOf(ref.result.loads_beyond);
	else if (use != ClockUse::NetworkLatency && output_side && !ref.result.barrier_a_pins.empty())
		ref.use_objects = handlesOf(ref.result.barrier_a_pins);
	else if (use == ClockUse::SourceLatency && ref.origin.hier_pin_dir == PortDir::Input && !ref.result.barrier_y_pins.empty())
		ref.use_objects = handlesOf(ref.result.barrier_y_pins);
	else
		return;
	ref.inside_handled = true;
}

// the loads inside a hierarchical input pin, with the y pins of its barrier instance
std::vector<ysta::Handle> loadsInside(const SdcObjectRef &ref)
{
	std::vector<ysta::Handle> loads;
	if (ref.stillAsLeafPins())
		loads = handlesOf(ref.pins);
	else
		loadsBehindPins(ref.pins, loads);
	if (loads.empty())
		return handlesOf(ref.pins);
	std::vector<ysta::Handle> barrier_y_pins = handlesOf(ref.result.barrier_y_pins);
	loads.insert(loads.end(), barrier_y_pins.begin(), barrier_y_pins.end());
	return loads;
}

// the leaf loads inside hierarchical pins
void loadsBehindPins(const std::vector<const Netlist::Pin *> &pins, std::vector<ysta::Handle> &loads)
{
	for (const Netlist::Pin *pin : pins) {
		if (pin->inner == nullptr)
			continue;
		std::vector<Netlist::Pin *> leaves = the_sta->view.leafPins(pin->inner, Netlist::WalkTarget::Loads);
		std::vector<ysta::Handle> inner = handlesOf(leaves);
		loads.insert(loads.end(), inner.begin(), inner.end());
	}
}

// write_sdc writes a clock name raw in braces, {a b} for the clock a b
void quoteClocks(std::vector<SdcWord> &words)
{
	for (SdcWord &word : words) {
		std::vector<SdcWord> &inner = word.nested_words;
		quoteClocks(inner);
		if (inner.size() != 2 || inner[0].text != "get_clocks")
			continue;
		std::string name = inner[1].text;
		if (name.size() > 2 && name.front() == '{' && name.back() == '}')
			name = name.substr(1, name.size() - 2);
		if (the_sta->cmdMode()->sdc()->findClock(name.c_str()) != nullptr)
			inner[1].text = tclQuoted(tclQuoted(name));
	}
}

} // namespace opensta_net
