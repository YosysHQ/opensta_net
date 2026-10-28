// the words on the objects that passes moved, then the clock names, in the mode of the command
// (a removed hierarchical pin goes on the leaf pins that opensta takes for it)
#include "opensta_net.h"

#include "sta/Mode.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

void rewriteWords(std::vector<SdcWord> &words)
{
	if (words.empty())
		return;
	disableWireEdges(words);
	hierPinLoads(words);
	applyClockImages(words);
	quoteClocks(words);
}

// a removed hierarchical pin or cell: the wire edges into the loads beyond its barriers
void disableWireEdges(std::vector<SdcWord> &words)
{
	if (words[0].text != "set_disable_timing")
		return;
	for (SdcWord &word : words) {
		if (word.refs.size() != 1)
			continue;
		SdcObjectRef &ref = the_sta->epoch.constraints.refs[word.refs[0]];
		bool removed = ref.kind == SdcObjectRef::ObjectKind::Cell && ref.objects.empty() && !ref.result.loads_beyond.empty();
		if (!ref.stillAsLeafPins() && !removed)
			continue;
		std::string handles;
		for (const void *pin : ref.result.loads_beyond)
			handles += " " + swigHandleOf(SdcObjectRef::ObjectKind::Pin, pin);
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
		for (uint32_t index : words[i].refs) {
			SdcObjectRef &ref = the_sta->epoch.constraints.refs[index];
			if (ref.stillAsLeafPins() && ref.facts.hier_dir == Netlist::Dir::Output)
				ref.use_objects = ref.result.loads_beyond;
			ref.inside_handled = true;
		}
	}
}

bool hasFlag(const std::vector<SdcWord> &words, const std::string &flag)
{
	for (const SdcWord &word : words) {
		if (word.text == flag)
			return true;
	}
	return false;
}

void applyClockImages(const std::vector<SdcWord> &words)
{
	if (isClockCommand(words[0].text))
		clockImages(words, ClockUse::Clock);
	else if (words[0].text == "set_clock_latency" && hasFlag(words, "-source"))
		clockImages(words, ClockUse::SourceLatency);
	else if (words[0].text == "set_clock_latency")
		clockImages(words, ClockUse::NetworkLatency);
}

void clockImages(const std::vector<SdcWord> &words, ClockUse use)
{
	for (size_t i = 0; i < words.size(); i++) {
		ClockUse word_use = use;
		if (use == ClockUse::Clock && i > 0 && words[i - 1].text == "-source")
			word_use = ClockUse::GeneratedClockSource;
		for (uint32_t index : words[i].refs)
			pickClockImage(the_sta->epoch.constraints.refs[index], word_use);
		clockImages(words[i].nested_words, use);
	}
}

// where a clock use of a hierarchical pin goes when flatten removed the pin
void pickClockImage(SdcObjectRef &ref, ClockUse use)
{
	using Dir = Netlist::Dir;
	bool output_side = ref.facts.hier_dir == Dir::Output || use == ClockUse::GeneratedClockSource;
	if (use == ClockUse::Clock && ref.facts.hier_dir == Dir::Input)
		ref.use_objects = loadsInside(ref);
	else if (!ref.stillAsLeafPins())
		return;
	else if (use == ClockUse::NetworkLatency && !ref.result.loads_beyond.empty())
		ref.use_objects = ref.result.loads_beyond;
	else if (use != ClockUse::NetworkLatency && output_side && !ref.result.image_a_pins.empty())
		ref.use_objects = ref.result.image_a_pins;
	else if (use == ClockUse::SourceLatency && ref.facts.hier_dir == Dir::Input && !ref.result.image_y_pins.empty())
		ref.use_objects = ref.result.image_y_pins;
	else
		return;
	ref.inside_handled = true;
}

// the loads inside a hierarchical input pin, with the y pins of its image
std::vector<const void *> loadsInside(const SdcObjectRef &ref)
{
	std::vector<const void *> loads;
	if (ref.stillAsLeafPins())
		loads = ref.objects;
	else
		addInnerLoads(ref.objects, loads);
	if (loads.empty())
		return ref.objects;
	const std::vector<const void *> &image_y_pins = ref.result.image_y_pins;
	loads.insert(loads.end(), image_y_pins.begin(), image_y_pins.end());
	return loads;
}

void addInnerLoads(const std::vector<const void *> &pins, std::vector<const void *> &loads)
{
	for (const void *object : pins) {
		const Netlist::Pin *pin = static_cast<const Netlist::Pin *>(object);
		if (pin->term != nullptr)
			the_sta->views.view->innerPins(pin->term->net, WalkTarget::Loads, loads);
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
