// the words on the objects that passes moved
// (a removed hierarchical pin goes on the leaf pins that opensta takes for it)
#include "opensta_net.h"

USING_YOSYS_NAMESPACE

namespace opensta_net {

bool hasFlag(const std::vector<Word> &words, const std::string &flag)
{
	for (const Word &word : words) {
		if (word.text == flag)
			return true;
	}
	return false;
}

void applyClockImages(const std::vector<Word> &words)
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

void clockImages(const std::vector<Word> &words, ClockUse use)
{
	for (size_t i = 0; i < words.size(); i++) {
		bool source = i > 0 && words[i - 1].text == "-source";
		for (uint32_t index : words[i].refs)
			clockImage(the_sta->constraints.refs[index], use, source);
		clockImages(words[i].nested_words, use);
	}
}

// where a clock use of a hierarchical pin goes when flatten removed the pin
void clockImage(Ref &ref, ClockUse use, bool source)
{
	using Dir = Netlist::Dir;
	if (use == ClockUse::Clock && ref.facts.hier_dir == Dir::Input && !source)
		ref.use = loadsInside(ref);
	else if (!ref.result.found_as_leaf_pins)
		return;
	else if (use == ClockUse::NetworkLatency && !ref.result.loads_beyond.empty())
		ref.use = ref.result.loads_beyond;
	else if (use != ClockUse::NetworkLatency && (ref.facts.hier_dir == Dir::Output || source) && !ref.result.image_a_pins.empty())
		ref.use = ref.result.image_a_pins;
	else if (use == ClockUse::SourceLatency && ref.facts.hier_dir == Dir::Input && !ref.result.image_y_pins.empty())
		ref.use = ref.result.image_y_pins;
	else
		return;
	ref.result.found_as_leaf_pins = false;
}

// with the y pins of the image
std::vector<const void *> loadsInside(const Ref &ref)
{
	std::vector<const void *> loads;
	if (ref.result.found_as_leaf_pins)
		loads = ref.objects;
	else
		addInnerLoads(ref.objects, loads);
	if (loads.empty())
		return ref.objects;
	loads.insert(loads.end(), ref.result.image_y_pins.begin(), ref.result.image_y_pins.end());
	return loads;
}

void addInnerLoads(const std::vector<const void *> &pins, std::vector<const void *> &loads)
{
	for (const void *object : pins) {
		const Netlist::Pin *pin = static_cast<const Netlist::Pin *>(object);
		if (pin->term != nullptr)
			the_sta->view->innerPins(pin->term->net, WalkTarget::Loads, loads);
	}
}

} // namespace opensta_net
