// sdc text to commands of words and back:
// - parse   write_sdc writes objects as tokens, each token of a getter becomes a ref
// - render  the words as a script on the objects that resolve found
#include "opensta_net.h"

USING_YOSYS_NAMESPACE

namespace opensta_net {

bool SdcObjectRef::stillAsLeafPins() const
{
	return result.found_as_leaf_pins && !inside_handled;
}

std::vector<std::string> sdcCommands(const std::string &text)
{
	std::vector<std::string> commands;
	std::stringstream in(text);
	std::string line, cmd;
	while (std::getline(in, line)) {
		cmd += line + "\n";
		if ((!line.empty() && line.back() == '\\') || !Tcl_CommandComplete(cmd.c_str()))
			continue;
		std::string normal = normalizeCommand(cmd);
		if (!normal.empty() && normal[0] != '#')
			commands.push_back(cmd);
		cmd.clear();
	}
	// incomplete: its evaluation reports the error
	if (!normalizeCommand(cmd).empty())
		commands.push_back(cmd);
	return commands;
}

// line continuations and whitespace runs collapsed
std::string normalizeCommand(const std::string &cmd)
{
	std::string out;
	bool space = false;
	for (size_t i = 0; i < cmd.size(); i++) {
		if (cmd[i] == '\\' && i + 1 < cmd.size() && cmd[i + 1] == '\n') {
			space = true;
			i++;
		} else if (isspace(static_cast<unsigned char>(cmd[i]))) {
			space = true;
		} else {
			if (space && !out.empty())
				out += ' ';
			space = false;
			out += cmd[i];
		}
	}
	return out;
}

std::string commandName(const std::string &script)
{
	Tcl_Parse parse;
	if (Tcl_ParseCommand(nullptr, script.data(), script.size(), 0, &parse) != TCL_OK)
		return "";
	std::string name;
	if (parse.numWords > 0)
		name = std::string(parse.tokenPtr->start, parse.tokenPtr->size);
	Tcl_FreeParse(&parse);
	return name;
}

bool isClockCommand(const std::string &name)
{
	return name == "create_clock" || name == "create_generated_clock";
}

bool isThroughFlag(const std::string &word)
{
	return word == "-through" || word == "-rise_through" || word == "-fall_through";
}

bool parseWords(const std::string &script, std::vector<SdcWord> &words)
{
	Tcl_Parse parse;
	if (Tcl_ParseCommand(nullptr, script.data(), script.size(), 0, &parse) != TCL_OK)
		return false;
	Tcl_Token *token = parse.tokenPtr;
	for (int w = 0; w < parse.numWords; w++) {
		SdcWord &word = words.emplace_back();
		word.text = std::string(token->start, token->size);
		bool substitution = token->numComponents == 1 && token[1].type == TCL_TOKEN_COMMAND;
		if (substitution)
			parseSubstitution(std::string(token[1].start + 1, token[1].size - 2), word);
		token += token->numComponents + 1;
	}
	Tcl_FreeParse(&parse);
	return true;
}

void parseSubstitution(const std::string &inner, SdcWord &word)
{
	std::string name = commandName(inner);
	// splitnets -ports renames ports, so they are refs too
	if (name == "get_pins" || name == "get_cells" || name == "get_nets" || name == "get_ports") {
		addGetterRefs(inner, word);
		return;
	}
	// library objects keep their names
	if (name == "get_lib_cells" || name == "get_lib_pins" || name == "get_libs")
		return;
	if (parseWords(inner, word.nested_words))
		word.kind = WordKind::Substitution;
}

void addGetterRefs(const std::string &inner, SdcWord &word)
{
	EpochConstraints &constraints = the_sta->epoch.constraints;
	for (auto [kind, object] : tokensIn(inner)) {
		if (kind == SdcObjectRef::ObjectKind::Port) {
			ysta::YosysNetwork *network = the_sta->yosys_network;
			object = network->findPin(network->topInstance(), static_cast<const sta::Port *>(object));
		}
		if (object == nullptr)
			continue;
		word.refs.push_back(constraints.refs.size());
		constraints.refs.push_back({{kind}, objectName(kind, object)});
		constraints.refs.back().objects = {object};
		constraints.view_refs.push_back(&constraints.refs.back());
	}
	if (!word.refs.empty())
		word.kind = WordKind::Getter;
}

void appendTraceTexts(SdcTextByMode &modes, const char *var)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	int count = 0;
	Tcl_Obj **items = nullptr;
	if (!tclEval(std::string("::ysta::texts ") + var) || Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &count, &items) != TCL_OK)
		return;
	for (int i = 0; i + 1 < count; i += 2)
		modes[Tcl_GetString(items[i])] += Tcl_GetString(items[i + 1]);
}

void markRefs(const std::vector<SdcWord> &words, bool SdcObjectRef::Request::*flag)
{
	for (uint32_t index : refsOf(words))
		the_sta->epoch.constraints.refs[index].request.*flag = true;
}

std::vector<ysta::Token> tokensIn(const std::string &text)
{
	std::vector<ysta::Token> found;
	const std::vector<ysta::Token> &tokens = the_sta->epoch.tokens;
	size_t at = 0;
	while ((at = text.find(ysta::detail::token_prefix, at)) != std::string::npos) {
		at += ysta::detail::token_prefix.size();
		size_t index = 0;
		size_t digits = 0;
		while (at < text.size() && isdigit(static_cast<unsigned char>(text[at]))) {
			index = index * 10 + (text[at++] - '0');
			digits++;
		}
		if (digits > 0 && index < tokens.size())
			found.push_back(tokens[index]);
	}
	return found;
}

std::string tokensToNames(const std::string &text)
{
	const std::vector<ysta::Token> &tokens = the_sta->epoch.tokens;
	std::string out;
	size_t at = 0;
	size_t next;
	while ((next = text.find(ysta::detail::token_prefix, at)) != std::string::npos) {
		out += text.substr(at, next - at);
		size_t end = next + ysta::detail::token_prefix.size();
		size_t index = 0;
		while (end < text.size() && isdigit(static_cast<unsigned char>(text[end])))
			index = index * 10 + (text[end++] - '0');
		if (index < tokens.size())
			out += tokenName(tokens[index]);
		at = end;
	}
	return out + text.substr(at);
}

std::string objectName(SdcObjectRef::ObjectKind kind, const void *object)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	switch (kind) {
	case SdcObjectRef::ObjectKind::Pin:
	case SdcObjectRef::ObjectKind::Port: return network->pathName(static_cast<const sta::Pin *>(object));
	case SdcObjectRef::ObjectKind::Cell: return network->pathName(static_cast<const sta::Instance *>(object));
	case SdcObjectRef::ObjectKind::Net: return network->pathName(static_cast<const sta::Net *>(object));
	}
	return "";
}

std::string tokenName(const ysta::Token &token)
{
	if (token.kind != SdcObjectRef::ObjectKind::Port)
		return objectName(token.kind, token.object);
	return the_sta->yosys_network->sta::ConcreteNetwork::name(static_cast<const sta::Port *>(token.object));
}

std::vector<uint32_t> refsOf(const std::vector<SdcWord> &words)
{
	std::vector<uint32_t> refs;
	for (const SdcWord &word : words) {
		refs.insert(refs.end(), word.refs.begin(), word.refs.end());
		std::vector<uint32_t> nested = refsOf(word.nested_words);
		refs.insert(refs.end(), nested.begin(), nested.end());
	}
	return refs;
}

std::string tclQuoted(const std::string &text)
{
	const char *argv[] = {text.c_str()};
	char *merged = Tcl_Merge(1, argv);
	std::string quoted = merged;
	Tcl_Free(merged);
	return quoted;
}

std::string swigHandleOf(SdcObjectRef::ObjectKind kind, const void *object)
{
	unsigned char bytes[sizeof(void *)];
	memcpy(bytes, &object, sizeof(bytes));
	std::string handle = "_";
	for (unsigned char byte : bytes)
		handle += stringf("%02x", byte);
	const sta::Network *network = the_sta->sdcNetwork();
	std::string name, getter;
	if (kind == SdcObjectRef::ObjectKind::Pin) {
		handle += "_p_Pin";
		name = network->pathName(static_cast<const sta::Pin *>(object));
		getter = "get_pins";
	} else if (kind == SdcObjectRef::ObjectKind::Cell) {
		handle += "_p_Instance";
		name = network->pathName(static_cast<const sta::Instance *>(object));
		getter = "get_cells";
	} else {
		handle += "_p_Net";
		name = network->pathName(static_cast<const sta::Net *>(object));
		getter = "get_nets";
	}
	Tcl_Interp *interp = yosys_get_tcl_interp();
	Tcl_CmdInfo info;
	// make the swig command of the handle
	if (!Tcl_GetCommandInfo(interp, handle.c_str(), &info))
		Tcl_EvalEx(interp, (getter + " -quiet " + tclQuoted(name)).c_str(), -1, TCL_EVAL_GLOBAL);
	return handle;
}

// a -through net that flatten grew takes its pins in the old scope
bool rendersThroughPins(const SdcObjectRef &ref, ThroughContext context)
{
	return context == ThroughContext::AfterThrough && !ref.result.through_pins.empty();
}

// the objects of a use beat the found objects
const std::vector<const void *> &objectsToRender(const SdcObjectRef &ref, ThroughContext context)
{
	if (rendersThroughPins(ref, context))
		return ref.result.through_pins;
	if (!ref.use_objects.empty())
		return ref.use_objects;
	return ref.objects;
}

std::string refHandles(SdcObjectRef &ref, ThroughContext context)
{
	if (context == ThroughContext::OutsideThrough)
		ref.used_outside_through = true;
	SdcObjectRef::ObjectKind kind = ref.kind;
	if (rendersThroughPins(ref, context))
		kind = SdcObjectRef::ObjectKind::Pin;
	std::string handles;
	for (const void *object : objectsToRender(ref, context)) {
		if (kind == SdcObjectRef::ObjectKind::Port)
			handles += " [get_ports " + tclQuoted(objectName(kind, object)) + "]";
		else
			handles += " " + swigHandleOf(kind, object);
	}
	return handles;
}

std::string renderWords(const std::vector<SdcWord> &words, int &empty_getters, ThroughContext context)
{
	std::string text;
	bool after_through_flag = false;
	for (const SdcWord &word : words) {
		if (!text.empty())
			text += ' ';
		ThroughContext word_context = context;
		if (after_through_flag)
			word_context = ThroughContext::AfterThrough;
		if (word.kind == WordKind::Getter) {
			text += renderGetter(word, empty_getters, word_context);
		} else if (word.kind == WordKind::Substitution) {
			text += "[" + renderWords(word.nested_words, empty_getters, word_context) + "]";
		} else {
			text += word.text;
		}
		after_through_flag = isThroughFlag(word.text);
	}
	return text;
}

std::string renderGetter(const SdcWord &word, int &empty_getters, ThroughContext context)
{
	std::string handles;
	for (uint32_t index : word.refs)
		handles += refHandles(the_sta->epoch.constraints.refs[index], context);
	empty_getters += handles.empty() && !onlyImagePins(word);
	return "[list" + handles + "]";
}

} // namespace opensta_net
