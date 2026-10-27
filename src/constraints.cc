// before each pass or rtlil edit:
// 1. write_sdc writes the constraints, objects as tokens
// 2. each token becomes a ref, which anchors its rtlil objects
// 3. passes carry the anchors
// 4. the rebuild resolves the refs and applies the commands clocks and library objects apply by name
#include "opensta_net.h"

#include "sta/Mode.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

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

bool isThroughFlag(const std::string &word)
{
	return word == "-through" || word == "-rise_through" || word == "-fall_through";
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

void markRefs(const std::vector<Word> &words, bool Ref::Request::*flag)
{
	for (uint32_t index : refsOf(words))
		the_sta->constraints.refs[index].request.*flag = true;
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

bool injectConstraints()
{
	ModeTexts modes;
	the_sta->tokens.clear();
	the_sta->yosys_network->setTokens(&the_sta->tokens);
	bool written = writeAllModes(modes);
	if (!written)
		addJournal(modes);
	the_sta->yosys_network->setTokens(nullptr);
	// a failed write_sdc keeps the last constraints and the journal
	if (written)
		the_sta->constraints = {};
	bool journaled = written || tclInt("expr {$::ysta::untexted == 0}") == 1;
	tclEval("set ::ysta::journal {}");
	for (const auto &[mode, sdc] : modes) {
		for (const std::string &text : sdcCommands(sdc)) {
			if (commandName(text) == "current_design")
				continue;
			Command &command = the_sta->constraints.commands.emplace_back();
			command.mode = mode;
			command.text = text;
			parseWords(text, command.words);
			if (isClockCommand(commandName(text)))
				markRefs(command.words, &Ref::Request::wants_image);
			command.log_text = untoken(text);
		}
	}
	the_sta->view->closeEpoch(the_sta->constraints.view_refs, the_sta->protection);
	return journaled;
}

std::string tclQuoted(const std::string &text)
{
	const char *argv[] = {text.c_str()};
	char *merged = Tcl_Merge(1, argv);
	std::string quoted = merged;
	Tcl_Free(merged);
	return quoted;
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

bool isClockCommand(const std::string &name)
{
	return name == "create_clock" || name == "create_generated_clock";
}

// refs that flatten moved inside, a -through or a clock on an input pin keeps its meaning
std::string movedInside(const std::vector<Word> &words)
{
	bool clock = !words.empty() && isClockCommand(words[0].text);
	std::string names;
	for (uint32_t index : refsOf(words)) {
		const Ref &ref = the_sta->constraints.refs[index];
		if (ref.result.found_as_leaf_pins && ref.plain && !(clock && ref.facts.hier_dir == Netlist::Dir::Input))
			names += (names.empty() ? "" : " ") + ref.name;
	}
	return names;
}

bool anyUnprotected(const std::vector<Word> &words)
{
	for (uint32_t index : refsOf(words)) {
		if (the_sta->constraints.refs[index].facts.moved_to_drivers)
			return true;
	}
	return false;
}

bool foundAny(const std::vector<Word> &words)
{
	for (uint32_t index : refsOf(words)) {
		if (!the_sta->constraints.refs[index].objects.empty())
			return true;
	}
	return false;
}

// a getter of image pins only, opensta ignores their clock attributes
bool onlyImages(const Word &word)
{
	for (uint32_t index : word.refs) {
		if (!the_sta->constraints.refs[index].facts.is_image_pin)
			return false;
	}
	return true;
}

std::string missingNames(const std::vector<Word> &words)
{
	std::string names;
	for (uint32_t index : refsOf(words)) {
		const Ref &ref = the_sta->constraints.refs[index];
		if (ref.objects.empty() && !ref.facts.is_image_pin)
			names += (names.empty() ? "" : " ") + ref.name;
	}
	return names;
}

bool applyCommand(const Command &command, std::string &reason)
{
	Tcl_Interp *interp = yosys_get_tcl_interp();
	YosysStaReport *report = static_cast<YosysStaReport *>(the_sta->report());
	int empty_getters = 0;
	applyClockImages(command.words);
	std::string script = command.words.empty() ? command.text : render(command.words, empty_getters, false);
	std::string missing = missingNames(command.words);
	std::string moved = movedInside(command.words);
	// an empty getter would widen a path exception
	if (empty_getters > 0) {
		reason = "no object found for " + missing;
		return false;
	}
	if (!refsOf(command.words).empty() && !foundAny(command.words)) {
		reason = "-unprotect removed the barrier that kept its hierarchical pin";
		return false;
	}
	int warnings = report->warnings;
	if (Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
		reason = std::string("failed: ") + Tcl_GetStringResult(interp);
		return false;
	}
	if (report->warnings != warnings) {
		reason = "applied with a warning";
		return false;
	}
	if (!missing.empty()) {
		reason = "applied without " + missing;
		return false;
	}
	return keptMeaning(command.words, moved, reason);
}

bool keptMeaning(const std::vector<Word> &words, const std::string &moved, std::string &reason)
{
	if (anyUnprotected(words)) {
		reason = "applied on the drivers of a hierarchical pin whose barrier -unprotect removed";
		return false;
	}
	if (!moved.empty()) {
		reason = "applied, flatten moved it to the leaf pins inside: " + moved;
		return false;
	}
	return true;
}

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

int tclInt(const std::string &script)
{
	tclEval(script);
	return atoi(Tcl_GetStringResult(yosys_get_tcl_interp()));
}

int applyConstraints()
{
	Constraints &constraints = the_sta->constraints;
	ysta::YosysNetwork *network = the_sta->yosys_network;
	for (Ref &ref : constraints.refs) {
		ref.request.top_port_pin = ref.kind == Ref::ObjectKind::Port ? network->findPin(network->topInstance(), ref.name) : nullptr;
		ref.plain = false;
		ref.use.clear();
	}
	the_sta->view->resolve(constraints.view_refs);
	std::string cmd_mode = the_sta->cmdMode()->name();
	sta::Scene *cmd_scene = the_sta->cmdScene();
	tclEval("set ::ysta::script [info script]; info script {}");
	int lost = 0;
	for (const Command &command : the_sta->constraints.commands) {
		the_sta->setCmdMode(command.mode);
		std::string reason;
		if (applyCommand(command, reason))
			continue;
		lost++;
		log_warning("opensta_net: sync: `%s` lost or changed: %s\n", normalizeCommand(command.log_text).c_str(), reason.c_str());
	}
	the_sta->setCmdMode(cmd_mode);
	the_sta->setCmdScene(cmd_scene);
	tclEval("info script $::ysta::script");
	tclEval("set ::ysta::journal {}");
	return lost;
}

} // namespace opensta_net
