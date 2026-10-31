// sdc text to commands of words and back:
// - parse   write_sdc writes objects as tokens, each token of a getter becomes a ref
// - render  the words as a script on the objects that resolve found
#include "opensta_net.h"

#include <charconv>

USING_YOSYS_NAMESPACE

namespace opensta_net {

bool SdcObjectRef::stillAsLeafPins() const
{
	return result.flattened_hier_pin && !inside_handled;
}

std::vector<const void *> SdcObjectRef::objects() const
{
	switch (kind) {
	case Kind::Instance: return asObjects(instances);
	case Kind::Net: return asObjects(nets);
	case Kind::Pin:
	case Kind::TopPort: return asObjects(pins);
	}
	return {};
}

void SdcObjectRef::addObject(const void *object)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	switch (kind) {
	case Kind::Instance: instances.push_back(network->asInst(static_cast<const sta::Instance *>(object))); return;
	case Kind::Net: nets.push_back(network->asNet(static_cast<const sta::Net *>(object))); return;
	case Kind::Pin:
	case Kind::TopPort: pins.push_back(network->asPin(static_cast<const sta::Pin *>(object))); return;
	}
}

const void *handleOf(const Netlist::Instance *inst)
{
	return the_sta->yosys_network->handle(inst);
}

const void *handleOf(const Netlist::Pin *pin)
{
	return the_sta->yosys_network->handle(pin);
}

const void *handleOf(const Netlist::Net *net)
{
	return the_sta->yosys_network->handle(net);
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

// get_timing_edges -from X -to Y
bool isEdgePair(const std::vector<SdcWord> &words)
{
	return words.size() == 5 && words[0].text == "get_timing_edges" && words[1].text == "-from" && words[3].text == "-to";
}

bool isGetterList(const std::vector<SdcWord> &words)
{
	for (size_t i = 1; i < words.size(); i++) {
		if (words[i].kind != WordKind::Getter)
			return false;
	}
	return words.size() > 1 && words[0].text == "list";
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
		// write_sdc writes some objects raw (set_voltage a net)
		else if (namesObject(word.text))
			addGetterRefs(word.text, word);
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
	if (isEdgePair(word.nested_words))
		markRefs(word.nested_words, RefMark::IsolateCell);
	// R4: a list of getters is one getter, so the rewrites see its refs
	if (isGetterList(word.nested_words)) {
		word.refs = refsOf(word.nested_words);
		word.nested_words.clear();
		word.kind = WordKind::Getter;
	}
}

void addGetterRefs(const std::string &inner, SdcWord &word)
{
	EpochConstraints &constraints = the_sta->epoch.constraints;
	for (auto [kind, object] : tokensIn(inner)) {
		if (kind == SdcObjectRef::Kind::TopPort) {
			ysta::YosysNetwork *network = the_sta->yosys_network;
			object = network->findPin(network->topInstance(), static_cast<const sta::Port *>(object));
		}
		if (object == nullptr)
			continue;
		word.refs.push_back(constraints.refs.size());
		constraints.refs.push_back({{kind}, objectName(kind, object)});
		constraints.refs.back().addObject(object);
		constraints.view_refs.push_back(&constraints.refs.back());
	}
	if (!word.refs.empty())
		word.kind = WordKind::Getter;
}

void parseCommands(const SdcTextByMode &modes)
{
	for (const auto &[mode, sdc] : modes) {
		for (const std::string &text : sdcCommands(sdc)) {
			if (commandName(text) == "current_design")
				continue;
			SdcCommand &command = the_sta->epoch.constraints.commands.emplace_back();
			command.mode = mode;
			command.text = text;
			parseWords(text, command.words);
			joinName(command.words);
			if (isClockCommand(commandName(text)))
				markRefs(command.words, RefMark::KeepAsInstance);
			command.log_text = tokensToNames(text);
		}
	}
}

// adds the texts of the kept list or the journal (::ysta::texts), returns the count of commands with no text
int appendTraceTexts(SdcTextByMode &modes, const char *list)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	if (!tclEval(std::string("::ysta::texts ") + list))
		return 0;
	int size = 0;
	Tcl_Obj **result = nullptr;
	if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &size, &result) != TCL_OK || size != 2)
		return 0;
	int count = 0;
	Tcl_Obj **items = nullptr;
	if (Tcl_ListObjGetElements(interp, result[0], &count, &items) != TCL_OK)
		return 0;
	for (int i = 0; i + 1 < count; i += 2)
		modes[Tcl_GetString(items[i])] += Tcl_GetString(items[i + 1]);
	int untexted = 0;
	Tcl_GetIntFromObj(nullptr, result[1], &untexted);
	return untexted;
}

// write_sdc writes a -name raw up to the next option, a journal or kept text braces it
void joinName(std::vector<SdcWord> &words)
{
	size_t at = 0;
	while (at < words.size() && words[at].text != "-name")
		at++;
	size_t end = at + 1;
	while (end < words.size() && !words[end].text.starts_with("-") && words[end].kind == WordKind::Plain)
		end++;
	bool braced = end == at + 2 && words[at + 1].text.starts_with("{");
	if (end <= at + 1 || braced)
		return;
	std::string name = words[at + 1].text;
	for (size_t i = at + 2; i < end; i++)
		name += " " + words[i].text;
	words[at + 1] = SdcWord{tclQuoted(name)};
	words.erase(words.begin() + at + 2, words.begin() + end);
}

void markRefs(const std::vector<SdcWord> &words, RefMark mark)
{
	for (uint32_t index : refsOf(words)) {
		SdcObjectRef &ref = the_sta->epoch.constraints.refs[index];
		if (mark == RefMark::IsolateCell)
			ref.hints.keep_cell = true;
		else
			ref.hints.keep_as_instance = true;
	}
}

// larger is tighter, the value is the last word, only -comment after it
bool tightness(const SdcCommand &command, double &tight)
{
	static const dict<std::string, double> signs = {{"set_min_delay", 1}, {"set_max_delay", -1}, {"set_multicycle_path", -1}};
	const std::vector<SdcWord> &words = command.words;
	if (words.empty() || !signs.count(words[0].text))
		return false;
	size_t end = words.size();
	if (end >= 2 && words[end - 2].text == "-comment")
		end -= 2;
	const std::string &text = words[end - 1].text;
	double value;
	auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc() || ptr != text.data() + text.size())
		return false;
	tight = signs.at(words[0].text) * value;
	return true;
}

// opensta takes the later of two exceptions on a path that flatten made them share
void orderOverrides(std::vector<SdcCommand> &commands)
{
	std::vector<std::tuple<int, double, size_t>> order;
	std::vector<size_t> places;
	int unsets = 0;
	for (size_t i = 0; i < commands.size(); i++) {
		double tight;
		if (!commands[i].words.empty() && commands[i].words[0].text == "unset_path_exceptions")
			unsets++;
		if (!tightness(commands[i], tight))
			continue;
		order.push_back({unsets, tight, i});
		places.push_back(i);
	}
	std::stable_sort(order.begin(), order.end());
	std::vector<SdcCommand> sorted;
	for (const auto &[run, tight, index] : order)
		sorted.push_back(std::move(commands[index]));
	for (size_t i = 0; i < places.size(); i++)
		commands[places[i]] = std::move(sorted[i]);
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

bool namesObject(const std::string &text)
{
	for (const auto &[kind, object] : tokensIn(text)) {
		if (kind != SdcObjectRef::Kind::TopPort)
			return true;
	}
	return false;
}

std::string objectName(SdcObjectRef::Kind kind, const void *object)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	switch (kind) {
	case SdcObjectRef::Kind::Pin:
	case SdcObjectRef::Kind::TopPort: return network->pathName(static_cast<const sta::Pin *>(object));
	case SdcObjectRef::Kind::Instance: return network->pathName(static_cast<const sta::Instance *>(object));
	case SdcObjectRef::Kind::Net: return network->pathName(static_cast<const sta::Net *>(object));
	}
	return "";
}

std::string tokenName(const ysta::Token &token)
{
	if (token.kind != SdcObjectRef::Kind::TopPort)
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

std::string swigHandleOf(SdcObjectRef::Kind kind, const void *object)
{
	unsigned char bytes[sizeof(void *)];
	memcpy(bytes, &object, sizeof(bytes));
	std::string handle = "_";
	for (unsigned char byte : bytes)
		handle += stringf("%02x", byte);
	const sta::Network *network = the_sta->sdcNetwork();
	std::string name, getter;
	if (kind == SdcObjectRef::Kind::Pin) {
		handle += "_p_Pin";
		name = network->pathName(static_cast<const sta::Pin *>(object));
		getter = "get_pins";
	} else if (kind == SdcObjectRef::Kind::Instance) {
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
	return context == ThroughContext::AfterThrough && !ref.result.old_scope_pins.empty();
}

// the exact pins beat the objects of a use, which beat the found objects
std::vector<const void *> objectsToRender(const SdcObjectRef &ref, ThroughContext context, PinChoice choice)
{
	if (rendersThroughPins(ref, context))
		return asObjects(ref.result.old_scope_pins);
	if (choice == PinChoice::ExactPins && !ref.result.own_pins.empty())
		return asObjects(ref.result.own_pins);
	if (!ref.use_objects.empty())
		return ref.use_objects;
	return ref.objects();
}

std::string refHandles(SdcObjectRef &ref, ThroughContext context, PinChoice choice)
{
	if (context == ThroughContext::OutsideThrough)
		ref.used_outside_through = true;
	SdcObjectRef::Kind kind = ref.kind;
	if (rendersThroughPins(ref, context))
		kind = SdcObjectRef::Kind::Pin;
	std::string handles;
	for (const void *object : objectsToRender(ref, context, choice)) {
		if (kind == SdcObjectRef::Kind::TopPort)
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
		} else if (word.kind == WordKind::Substitution && isEdgePair(word.nested_words)) {
			std::vector<SdcWord> pair = {word.nested_words[2], word.nested_words[4]};
			text += "[::ysta::edges " + renderWords(pair, empty_getters, word_context) + "]";
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
		handles += refHandles(the_sta->epoch.constraints.refs[index], context, word.pin_choice);
	empty_getters += handles.empty() && !onlyBarrierInstancePins(word);
	return "[list" + handles + "]";
}

} // namespace opensta_net
