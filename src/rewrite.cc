// commands as scripts on the objects passes changed, see renderCommand
#include "opensta_net.h"

#include "sta/Mode.hh"
#include "sta/Sdc.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

// renders on the found objects by handle, see refHandles
// a broken command stays as its text
// trace.tcl renders journal and kept commands via ::ysta::getter, tokens at capture
RenderedCommand renderCommand(const SdcCommand &command)
{
	RenderState render;
	const std::vector<SdcWord> &words = command.words;
	if (words.empty()) {
		render.result.script = command.text;
		return std::move(render.result);
	}
	for (size_t i = 0; i + 1 < words.size(); i++) {
		if (words[i].text == "-from")
			render.words_after_from.push_back(unwrapped(words[i + 1].text, '{', '}'));
	}
	for (size_t i = 0; i < words.size(); i++) {
		if (std::optional<std::string> flag_and_getter = renderFlagAndGetterBehindNewLogic(words, i, render)) {
			appendSpaced(render.result.script, *flag_and_getter);
			i++; // the getter went with its flag
		} else {
			appendSpaced(render.result.script, renderTopWord(words, i, render));
		}
	}
	noteMovedRefs(words, render);
	return std::move(render.result);
}

// refs flatten moved inside that no use put on stand-ins
// a clock on an input pin flatten moved inside keeps its meaning
// words is not empty, renderCommand checks
void noteMovedRefs(const std::vector<SdcWord> &words, RenderState &render)
{
	bool clock = isClockCommand(words[0].text);
	for (SdcObjectRef *ref : refsOf(words)) {
		bool clock_input = clock && ref->origin.hier_pin_dir == Netlist::PortDir::Input;
		render.result.to_drivers |= ref->origin.anchored_on_drivers;
		if (ref->result.flattened_hier_pin && !render.on_stand_ins.count(ref) && !clock_input)
			appendSpaced(render.result.moved, ref->name);
	}
}

// abc maps a gate to cells
// a -from or -to arc port takes its new port name
// the last matching rename wins
std::string renamedPort(const std::vector<SdcObjectRef *> &refs, const std::string &text)
{
	std::string old_name = unwrapped(text, '{', '}');
	std::string port;
	for (SdcObjectRef *ref : refs) {
		for (const auto &[old_port, old_bit, pin] : ref->result.port_renames) {
			if (old_port == old_name)
				port = "{" + pin->port().name + "}";
		}
	}
	return port;
}

std::vector<ysta::Handle> renamedCells(const SdcObjectRef &ref, const std::vector<std::string> &words_after_from)
{
	std::vector<ysta::Handle> cells;
	for (const std::string &from_port : words_after_from) {
		for (const auto &[old_port, old_bit, pin] : ref.result.port_renames) {
			if (old_port == from_port)
				cells.push_back(staNetwork().handle(pin->inst));
		}
	}
	return cells;
}

// a flag and its getter behind new logic, else none
std::optional<std::string> renderFlagAndGetterBehindNewLogic(const SdcWords &words, size_t at, RenderState &render)
{
	const SdcWord &flag = words[at];
	if (at + 1 == words.size() || !anyBehindNewLogic(words[at + 1]))
		return std::nullopt;
	const SdcWord &getter = words[at + 1];
	// -to behind new logic becomes -through the old signal -to the exact pins
	// e.g. a mux before D
	if (getter.use == Use::Endpoint) {
		std::string old_signal = renderGetter(getter, Use::Through, Pins::ByUse, render);
		std::string exact_pins = renderGetter(getter, Use::Through, Pins::Exact, render);
		return "-through " + old_signal + " " + flag.text + " " + exact_pins;
	}
	// -rise_from behind new logic takes the edge of the old signal
	// e.g. an inverter before C
	bool edge = flag.text == "-rise_from" || flag.text == "-fall_from";
	if (getter.use == Use::Start && edge) {
		std::string old_signal = renderGetter(getter, Use::Start, Pins::ByUse, render);
		std::string exact_pins = renderGetter(getter, Use::Start, Pins::Exact, render);
		return "[::ysta::start_edge " + flag.text + " " + old_signal + "] " + exact_pins;
	}
	return std::nullopt;
}

// a top level word, a getter behind new logic or renamed port
std::string renderTopWord(const std::vector<SdcWord> &words, size_t at, RenderState &render)
{
	const SdcWord &word = words[at];
	if (anyBehindNewLogic(word) && word.use == Use::Start)
		return renderGetter(word, Use::Start, Pins::Exact, render);
	std::string port;
	if (at > 0 && (words[at - 1].text == "-from" || words[at - 1].text == "-to"))
		port = renamedPort(refsOf(words), word.text);
	if (!port.empty())
		return port;
	return renderWord(word, render);
}

std::string disabledEdges(const SdcWord &word, RenderState &render)
{
	if (word.use != Use::Disable || word.refs.size() != 1)
		return "";
	SdcObjectRef &ref = *word.refs[0];
	if (!ref.result.flattened_hier_pin && !ref.removedButLoadsKept())
		return "";
	render.on_stand_ins.insert(&ref);
	return "[::ysta::wire_edges [list" + refHandles(ref, handlesOf(ref.result.loads_beyond)) + "]]";
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

std::vector<ysta::Handle> standIns(const SdcObjectRef &ref, Use use, const std::vector<ysta::Handle> &found)
{
	using PortDir = Netlist::PortDir;
	const TrackedRef::Result &result = ref.result;
	bool output = ref.origin.hier_pin_dir == PortDir::Output;
	if (use == Use::ReferencePin && !found.empty())
		return {found.front()};
	// also on a live hierarchical input, the cases below need a flattened pin
	if (use == Use::ClockPin && ref.origin.hier_pin_dir == PortDir::Input)
		return loadsInside(ref);
	if (!ref.result.flattened_hier_pin)
		return {};
	switch (use) {
	// -through or case value without loads beyond takes the found objects
	// so the ref does not count as moved
	case Use::Through:
	case Use::CaseValue:
		if (output && !result.loads_beyond.empty())
			return handlesOf(result.loads_beyond);
		return found;
	case Use::ClockPin:
	case Use::ClockSource: return handlesOf(result.barrier_a_pins);
	case Use::SourceLatency: return handlesOf(output ? result.barrier_a_pins : result.barrier_y_pins);
	case Use::NetworkLatency: return handlesOf(result.loads_beyond);
	default: return {};
	}
}

// the loads inside a hierarchical input pin, plus its barrier y pins
std::vector<ysta::Handle> loadsInside(const SdcObjectRef &ref)
{
	std::vector<ysta::Handle> loads;
	if (ref.result.flattened_hier_pin)
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
		const NetView &view = staNetwork().view();
		std::vector<Netlist::Pin *> leaves = view.leafPins(pin->inner, Netlist::WalkTarget::Loads);
		std::vector<ysta::Handle> inner = handlesOf(leaves);
		loads.insert(loads.end(), inner.begin(), inner.end());
	}
}

// a get_clocks of a known clock by its exact name
// write_sdc writes a name raw in braces, {a b} for clock a b
std::string clockGetter(const SdcWord &word, RenderState &render)
{
	if (word.nested_words.size() != 2 || word.nested_words[0].text != "get_clocks")
		return "";
	const std::string &text = word.nested_words[1].text;
	std::string name = unwrapped(text, '{', '}');
	if (the_sta->cmdMode()->sdc()->findClock(name.c_str()) == nullptr)
		return "[get_clocks " + text + "]";
	return "[get_clocks " + tclQuoted(tclQuoted(name)) + "]";
}

void appendSpaced(std::string &text, const std::string &word)
{
	if (!text.empty())
		text += ' ';
	text += word;
}

} // namespace opensta_net
