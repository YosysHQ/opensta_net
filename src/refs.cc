// refs of the captured tokens and their Tcl names
#include "opensta_net.h"

#include <charconv>

USING_YOSYS_NAMESPACE

namespace opensta_net {

// a wire is its loads, they have the paths through the old object, see WireAs
Handles SdcObjectRef::objects(WireAs wire_as) const
{
	bool takes_loads = wire_as != WireAs::None && wire_as != WireAs::Nets;
	if (isWire() && !takes_loads && kind != Kind::Pin)
		return {};
	// a net-only command takes no stand-in pins
	if (was_net && kind == Kind::Pin && wire_as == WireAs::Nets)
		return {};
	if (kind == Kind::Net)
		return staNetwork().handles(nets);
	if (isWire())
		return staNetwork().handles(loads);
	if (kind == Kind::Instance)
		return staNetwork().handles(instances);
	return staNetwork().handles(pins);
}

ysta::YosysNetwork &staNetwork() { return static_cast<ysta::YosysNetwork &>(*the_sta->network()); }

// a new ref has one object, the kernel resolve sets its result
void makeRefs(const ysta::TokenIndexes &tokens)
{
	const ysta::YosysNetwork &network = staNetwork();
	the_sta->refs.assign(tokens.size(), SdcObjectRef{});
	for (const auto &[handle, index] : tokens) {
		SdcObjectRef &ref = the_sta->refs[index];
		ref.name = objectName(&network, handle);
		switch (handle.kind()) {
		case ysta::Handle::Kind::Instance:
			ref.kind = SdcObjectRef::Kind::Instance;
			ref.instances.push_back(network.recordOf(handle, &NetView::instances));
			break;
		case ysta::Handle::Kind::Net:
			ref.kind = SdcObjectRef::Kind::Net;
			ref.nets.push_back(network.recordOf(handle, &NetView::nets));
			ref.was_hier = true;
			ref.was_net = true;
			break;
		case ysta::Handle::Kind::Pin:
			ref.kind = SdcObjectRef::Kind::Pin;
			ref.pins.push_back(network.recordOf(handle, &NetView::pins));
			ref.was_hier = !ref.pins[0]->inst->isLeaf();
			if (!network.isTopLevelPort(handle)) {
				ref.was_start = isPathEnd(handle, PathSide::Start);
				ref.was_end = isPathEnd(handle, PathSide::End);
			}
			break;
		case ysta::Handle::Kind::Term:
			log_abort(); // no token is of a Term
		}
	}
}

std::vector<TrackedRef *> trackedRefs()
{
	std::vector<TrackedRef *> tracked;
	for (SdcObjectRef &ref : the_sta->refs)
		tracked.push_back(&ref);
	return tracked;
}

SdcObjectRef *tokenRef(std::string_view word)
{
	std::string_view prefix = ysta::detail::token_prefix;
	if (!word.starts_with(prefix))
		return nullptr;
	size_t index = 0;
	const char *end = word.data() + word.size();
	auto [last, error] = std::from_chars(word.data() + prefix.size(), end, index);
	bool is_index = error == std::errc() && last == end;
	if (!is_index || index >= the_sta->refs.size())
		return nullptr;
	return &the_sta->refs[index];
}

// tokens as the user names of their refs
std::string namedText(const std::string &text)
{
	std::string_view prefix = ysta::detail::token_prefix;
	std::string named;
	size_t at = 0, next = 0;
	while ((next = text.find(prefix, at)) != std::string::npos) {
		size_t end = next + prefix.size();
		while (end < text.size() && isdigit(static_cast<unsigned char>(text[end])))
			end++;
		std::string token = text.substr(next, end - next);
		SdcObjectRef *ref = tokenRef(token);
		named += text.substr(at, next - at);
		if (ref != nullptr)
			named += ref->name;
		else
			named += token;
		at = end;
	}
	return named + text.substr(at);
}

std::vector<std::string> tclWords(Tcl_Obj *list)
{
	int count = 0;
	Tcl_Obj **items = nullptr;
	std::vector<std::string> words;
	// a word that is no list has no words
	if (Tcl_ListObjGetElements(nullptr, list, &count, &items) != TCL_OK)
		return words;
	for (int i = 0; i < count; i++)
		words.push_back(Tcl_GetString(items[i]));
	return words;
}

std::string objectName(const sta::Network *network, ysta::Handle handle)
{
	switch (handle.kind()) {
	case ysta::Handle::Kind::Instance: return network->pathName(static_cast<const sta::Instance *>(handle));
	case ysta::Handle::Kind::Net: return network->pathName(static_cast<const sta::Net *>(handle));
	default: return network->pathName(static_cast<const sta::Pin *>(handle));
	}
}

std::string tclQuoted(const std::string &text)
{
	const char *argv[] = {text.c_str()};
	char *merged = Tcl_Merge(1, argv);
	std::string quoted = merged;
	Tcl_Free(merged);
	return quoted;
}

// get_ports takes * and ? as wildcards, find_port the exact name
std::string objectHandle(ysta::Handle handle)
{
	// a getter walks each bus bit of the cell, a finder takes the exact name
	static const char *finders[] = {"::sta::find_instance ", "::sta::find_pin ", "::sta::find_net "};
	log_assert(handle.kind() != ysta::Handle::Kind::Term);
	Tcl_Interp *interp = the_sta->tclInterp();
	if (handle.kind() == ysta::Handle::Kind::Pin && staNetwork().isTopLevelPort(handle)) {
		std::string port_name = objectName(&staNetwork(), handle);
		std::string find_port = "::ysta::find_port " + tclQuoted(port_name);
		Tcl_EvalEx(interp, find_port.c_str(), -1, TCL_EVAL_GLOBAL);
		return Tcl_GetStringResult(interp);
	}
	std::string name = handle.swigName();
	Tcl_CmdInfo info;
	// the finder makes the swig command of the handle
	if (!Tcl_GetCommandInfo(interp, name.c_str(), &info)) {
		std::string object_name = objectName(&staNetwork(), handle);
		std::string finder = finders[int(handle.kind())] + tclQuoted(object_name);
		Tcl_EvalEx(interp, finder.c_str(), -1, TCL_EVAL_GLOBAL);
	}
	return name;
}

} // namespace opensta_net
