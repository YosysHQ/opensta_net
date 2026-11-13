// refs of the captured tokens and their Tcl names
#include "opensta_net.h"

#include <charconv>

USING_YOSYS_NAMESPACE

namespace opensta_net {

Handles SdcObjectRef::objects(WireAs wire_as) const
{
	if (isWire() && wire_as == WireAs::None && kind != Kind::Pin)
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
void makeRefs(const std::map<ysta::Handle, size_t> &tokens)
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
	if (error != std::errc() || last != end || index >= the_sta->refs.size())
		return nullptr;
	return &the_sta->refs[index];
}

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
		named += ref != nullptr ? ref->name : token;
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
	static const char *getters[] = {"get_cells", "get_pins", "get_nets"};
	log_assert(handle.kind() != ysta::Handle::Kind::Term);
	Tcl_Interp *interp = yosys_get_tcl_interp();
	if (handle.kind() == ysta::Handle::Kind::Pin && staNetwork().isTopLevelPort(handle)) {
		std::string port_name = objectName(&staNetwork(), handle);
		std::string find_port = "::ysta::find_port " + tclQuoted(port_name);
		Tcl_EvalEx(interp, find_port.c_str(), -1, TCL_EVAL_GLOBAL);
		return Tcl_GetStringResult(interp);
	}
	std::string name = handle.swigName();
	Tcl_CmdInfo info;
	// make the swig command of the handle
	if (!Tcl_GetCommandInfo(interp, name.c_str(), &info)) {
		std::string object_name = objectName(the_sta->sdcNetwork(), handle);
		std::string getter = getters[int(handle.kind())] + std::string(" -quiet ") + tclQuoted(object_name);
		Tcl_EvalEx(interp, getter.c_str(), -1, TCL_EVAL_GLOBAL);
	}
	return name;
}

} // namespace opensta_net
