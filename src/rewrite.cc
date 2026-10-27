// the words on the objects that passes moved
// (a removed hierarchical pin goes on the leaf pins that opensta takes for it)
#include "opensta_net.h"

USING_YOSYS_NAMESPACE

namespace opensta_net {

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
	if (words.empty())
		return;
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

} // namespace opensta_net
