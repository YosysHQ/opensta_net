// sdc text to commands of words and back:
// - parse   write_sdc writes objects as tokens, each token of a getter becomes a ref
// - render  the words as a script on the objects that resolve found
#include "opensta_net.h"

USING_YOSYS_NAMESPACE

namespace opensta_net {

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
	// Incomplete: its evaluation reports the error
	if (!normalizeCommand(cmd).empty())
		commands.push_back(cmd);
	return commands;
}

// Line continuations and whitespace runs collapsed
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

bool parseWords(const std::string &script, std::vector<Word> &words)
{
	Tcl_Parse parse;
	if (Tcl_ParseCommand(nullptr, script.data(), script.size(), 0, &parse) != TCL_OK)
		return false;
	Tcl_Token *token = parse.tokenPtr;
	for (int w = 0; w < parse.numWords; w++) {
		Word &word = words.emplace_back();
		word.text = std::string(token->start, token->size);
		bool substitution = token->numComponents == 1 && token[1].type == TCL_TOKEN_COMMAND;
		if (substitution)
			parseSubstitution(std::string(token[1].start + 1, token[1].size - 2), word);
		token += token->numComponents + 1;
	}
	Tcl_FreeParse(&parse);
	return true;
}

void parseSubstitution(const std::string &inner, Word &word)
{
	std::string name = commandName(inner);
	// splitnets -ports renames ports, so they are refs too
	if (name == "get_pins" || name == "get_cells" || name == "get_nets" || name == "get_ports") {
		parseGetter(inner, word);
		return;
	}
	// library objects keep their names
	if (name == "get_lib_cells" || name == "get_lib_pins" || name == "get_libs")
		return;
	word.nested = parseWords(inner, word.nested_words);
}

void parseGetter(const std::string &inner, Word &word)
{
	Constraints &constraints = the_sta->constraints;
	for (auto [kind, object] : tokensIn(inner)) {
		if (kind == Ref::ObjectKind::Port) {
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
}

void addJournal(ModeTexts &modes)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	int count = 0;
	Tcl_Obj **items = nullptr;
	if (!tclEval("::ysta::texts ::ysta::journal") || Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &count, &items) != TCL_OK)
		return;
	for (int i = 0; i + 1 < count; i += 2)
		modes[Tcl_GetString(items[i])] += Tcl_GetString(items[i + 1]);
}

void markRefs(const std::vector<Word> &words, bool Ref::Request::*flag)
{
	for (uint32_t index : refsOf(words))
		the_sta->constraints.refs[index].request.*flag = true;
}

std::vector<ysta::Token> tokensIn(const std::string &text)
{
	std::vector<ysta::Token> found;
	const std::vector<ysta::Token> &tokens = the_sta->tokens;
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

std::string untoken(const std::string &text)
{
	std::string out;
	size_t at = 0;
	size_t next;
	while ((next = text.find(ysta::detail::token_prefix, at)) != std::string::npos) {
		out += text.substr(at, next - at);
		size_t end = next + ysta::detail::token_prefix.size();
		size_t index = 0;
		while (end < text.size() && isdigit(static_cast<unsigned char>(text[end])))
			index = index * 10 + (text[end++] - '0');
		if (index < the_sta->tokens.size())
			out += tokenName(the_sta->tokens[index]);
		at = end;
	}
	return out + text.substr(at);
}

std::string objectName(Ref::ObjectKind kind, const void *object)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (kind == Ref::ObjectKind::Pin || kind == Ref::ObjectKind::Port)
		return network->pathName(static_cast<const sta::Pin *>(object));
	if (kind == Ref::ObjectKind::Cell)
		return network->pathName(static_cast<const sta::Instance *>(object));
	return network->pathName(static_cast<const sta::Net *>(object));
}

std::string tokenName(const ysta::Token &token)
{
	if (token.kind != Ref::ObjectKind::Port)
		return objectName(token.kind, token.object);
	return the_sta->yosys_network->sta::ConcreteNetwork::name(static_cast<const sta::Port *>(token.object));
}

std::vector<uint32_t> refsOf(const std::vector<Word> &words)
{
	std::vector<uint32_t> refs;
	for (const Word &word : words) {
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

std::string handleOf(Ref::ObjectKind kind, const void *object)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char bytes[sizeof(void *)];
	memcpy(bytes, &object, sizeof(bytes));
	std::string handle = "_";
	for (unsigned char byte : bytes) {
		handle += hex[byte >> 4];
		handle += hex[byte & 15];
	}
	const sta::Network *network = the_sta->sdcNetwork();
	std::string name, getter;
	if (kind == Ref::ObjectKind::Pin) {
		handle += "_p_Pin";
		name = network->pathName(static_cast<const sta::Pin *>(object));
		getter = "get_pins";
	} else if (kind == Ref::ObjectKind::Cell) {
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

std::string refHandles(Ref &ref, bool after_through)
{
	// a -through net that flatten grew takes its pins in the old scope
	bool use_through = !ref.result.through_pins.empty() && after_through;
	ref.plain = ref.plain || !after_through;
	Ref::ObjectKind kind = use_through ? Ref::ObjectKind::Pin : ref.kind;
	const std::vector<const void *> *objects = use_through ? &ref.result.through_pins : &ref.objects;
	if (!use_through && !ref.use.empty())
		objects = &ref.use;
	std::string handles;
	for (const void *object : *objects) {
		if (kind == Ref::ObjectKind::Port)
			handles += " [get_ports " + tclQuoted(objectName(kind, object)) + "]";
		else
			handles += " " + handleOf(kind, object);
	}
	return handles;
}

std::string render(const std::vector<Word> &words, int &empty_getters, bool after_through)
{
	std::string text;
	bool after_flag = false;
	for (const Word &word : words) {
		if (!text.empty())
			text += ' ';
		bool through = after_flag || after_through;
		if (!word.refs.empty()) {
			std::string handles;
			for (uint32_t index : word.refs)
				handles += refHandles(the_sta->constraints.refs[index], through);
			empty_getters += handles.empty() && !onlyImages(word);
			text += "[list" + handles + "]";
		} else if (word.nested) {
			text += "[" + render(word.nested_words, empty_getters, through) + "]";
		} else {
			text += word.text;
		}
		after_flag = isThroughFlag(word.text);
	}
	return text;
}

} // namespace opensta_net
