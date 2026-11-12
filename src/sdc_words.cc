// SDC text to commands of words and back
#include "opensta_net.h"

#include <charconv>

USING_YOSYS_NAMESPACE

namespace opensta_net {

Handles SdcObjectRef::objects() const
{
	switch (kind) {
	case Kind::Instance: return handlesOf(instances);
	case Kind::Net: return handlesOf(nets);
	case Kind::Pin:
	case Kind::TopPort: return handlesOf(pins);
	}
	return {};
}

void SdcObjectRef::addObject(ysta::Handle handle)
{
	switch (kind) {
	case Kind::Instance: instances.push_back(staNetwork().recordOf<Netlist::Instance>(handle)); return;
	case Kind::Net: nets.push_back(staNetwork().recordOf<Netlist::Net>(handle)); return;
	case Kind::Pin:
	case Kind::TopPort: pins.push_back(staNetwork().recordOf<Netlist::Pin>(handle)); return;
	}
}

ysta::YosysNetwork &staNetwork()
{
	return static_cast<ysta::YosysNetwork &>(*the_sta->network());
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
	// an incomplete command stays, its evaluation reports the error
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

// get_timing_edges -from X -to Y
bool isEdgePair(const SdcWords &words)
{
	if (words.size() != 5)
		return false;
	bool from_to = words[1].text == "-from" && words[3].text == "-to";
	return words[0].text == "get_timing_edges" && from_to;
}

bool isGetterList(const SdcWords &words)
{
	for (size_t i = 1; i < words.size(); i++) {
		if (words[i].kind != WordKind::Getter)
			return false;
	}
	return words.size() > 1 && words[0].text == "list";
}

bool SdcReader::parseWords(const std::string &script, SdcWords &words)
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

void SdcReader::parseSubstitution(const std::string &inner, SdcWord &word)
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
	// the arc ends of get_timing_edges keep their cell
	if (isEdgePair(word.nested_words)) {
		for (SdcObjectRef *ref : refsOf(word.nested_words))
			ref->hints.keep_cell = true;
	}
	// a list of getters is one getter, so the rewrites see its refs
	if (isGetterList(word.nested_words)) {
		word.refs = refsOf(word.nested_words);
		word.nested_words.clear();
		word.kind = WordKind::Getter;
	}
}

// a new ref has one object
// the parse sets its hints later, the kernel resolve sets its result
void SdcReader::addGetterRefs(const std::string &inner, SdcWord &word)
{
	for (auto [kind, handle] : tokensIn(inner)) {
		if (handle.bits == 0)
			continue;
		SdcObjectRef &ref = constraints.refs.emplace_back(SdcObjectRef{{kind}, userName(handle)});
		ref.addObject(handle);
		word.refs.push_back(&ref);
		constraints.view_refs.push_back(&ref);
	}
	if (!word.refs.empty())
		word.kind = WordKind::Getter;
}

void parseCommands(const SdcTextByMode &modes, const std::vector<ysta::Token> &tokens, EpochConstraints &into)
{
	SdcReader reader{tokens, into};
	for (const auto &[mode, sdc] : modes) {
		for (const std::string &text : sdcCommands(sdc)) {
			std::string name = commandName(text);
			if (name == "current_design")
				continue;
			SdcCommand &command = into.commands.emplace_back();
			SdcWords &words = command.words;
			command.mode = mode;
			reader.parseWords(text, words);
			joinName(words);
			bool latency_source = std::ranges::find(words, "-source", &SdcWord::text) != words.end();
			assignUses(words, name, latency_source);
			// the barrier of a clock command pin becomes a Barrier instance
			if (isClockCommand(name)) {
				for (SdcObjectRef *ref : refsOf(words))
					ref->hints.keep_as_instance = true;
			}
			reader.tokensIn(text, &command.log_text);
		}
	}
}

// adds the texts of the kept list or the journal, see ::ysta::texts
// returns the count of commands with no text
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

// write_sdc writes a -name raw up to the next option
// a journal or kept text braces it
void joinName(SdcWords &words)
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

// sets each word's use from the command and the flag before it
// recurses into nested words of a clock command or set_clock_latency
// all nested words of a -through get Through
void assignUses(SdcWords &words, const std::string &command, bool latency_source)
{
	std::string flag;
	for (SdcWord &word : words) {
		word.use = useOf(command, flag, latency_source);
		if (isClockCommand(command) || command == "set_clock_latency")
			assignUses(word.nested_words, command, latency_source);
		if (word.use == Use::Through)
			assignUse(word.nested_words, Use::Through);
		flag = word.text;
	}
}

void assignUse(SdcWords &words, Use use)
{
	for (SdcWord &word : words) {
		word.use = use;
		assignUse(word.nested_words, use);
	}
}

Use useOf(const std::string &command, const std::string &flag, bool latency_source)
{
	static const pool<std::string> path_commands = {
		"set_false_path", "set_multicycle_path", "set_max_delay",
		"set_min_delay", "set_data_check", "group_path"
	};
	bool from_flag = flag == "-from" || flag == "-rise_from" || flag == "-fall_from";
	bool to_flag = flag == "-to" || flag == "-rise_to" || flag == "-fall_to";
	bool through_flag = flag == "-through" || flag == "-rise_through" || flag == "-fall_through";
	if (isClockCommand(command))
		return flag == "-source" ? Use::ClockSource : Use::ClockPin;
	if (command == "set_clock_latency")
		return latency_source ? Use::SourceLatency : Use::NetworkLatency;
	if (through_flag)
		return Use::Through;
	if (command == "set_case_analysis")
		return Use::CaseValue;
	if (command == "set_disable_timing")
		return Use::Disable;
	if (flag == "-reference_pin")
		return Use::ReferencePin;
	if (path_commands.count(command) && to_flag && command != "set_data_check")
		return Use::Endpoint;
	if (path_commands.count(command) && (from_flag || to_flag))
		return Use::Start;
	return Use::Objects;
}

// larger is tighter, the value is the last word, only -comment after it
bool tightness(const SdcCommand &command, double &tight)
{
	static const dict<std::string, double> signs = {
		{"set_min_delay", 1},
		{"set_max_delay", -1},
		{"set_multicycle_path", -1}
	};
	const SdcWords &words = command.words;
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

// flatten can make two exceptions share a path
// OpenSTA takes the later one there
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

std::vector<ysta::Token> SdcReader::tokensIn(const std::string &text, std::string *names) const
{
	std::vector<ysta::Token> found;
	size_t at = 0;
	size_t next;
	while ((next = text.find(ysta::detail::token_prefix, at)) != std::string::npos) {
		size_t from = at;
		size_t digits = next + ysta::detail::token_prefix.size();
		size_t index = 0;
		for (at = digits; at < text.size() && isdigit(static_cast<unsigned char>(text[at])); at++)
			index = index * 10 + (text[at] - '0');
		bool known = at > digits && index < tokens.size();
		if (known)
			found.push_back(tokens[index]);
		if (names == nullptr)
			continue;
		*names += text.substr(from, next - from);
		if (known)
			*names += userName(tokens[index].handle);
		else
			*names += text.substr(next, at - next);
	}
	if (names != nullptr)
		*names += text.substr(at);
	return found;
}

bool SdcReader::namesObject(const std::string &text) const
{
	for (const auto &[kind, handle] : tokensIn(text)) {
		if (kind != SdcObjectRef::Kind::TopPort)
			return true;
	}
	return false;
}

std::string objectName(const sta::Network *network, ysta::Handle handle)
{
	switch (handle.kind()) {
	case ysta::Handle::Kind::Instance: return network->pathName(static_cast<const sta::Instance *>(handle));
	case ysta::Handle::Kind::Net: return network->pathName(static_cast<const sta::Net *>(handle));
	default: return network->pathName(static_cast<const sta::Pin *>(handle));
	}
}

// a Barrier instance pin gets the name of its sync ref
// a loss then names the user's object
std::string userName(ysta::Handle handle)
{
	auto found = the_sta->user_names.find(handle);
	if (found != the_sta->user_names.end())
		return found->second;
	return objectName(&staNetwork(), handle);
}

std::vector<SdcObjectRef *> refsOf(const SdcWords &words)
{
	std::vector<SdcObjectRef *> refs;
	for (const SdcWord &word : words) {
		refs.insert(refs.end(), word.refs.begin(), word.refs.end());
		std::vector<SdcObjectRef *> nested = refsOf(word.nested_words);
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

std::string swigHandle(ysta::Handle handle)
{
	static const char *getters[] = {"get_cells", "get_pins", "get_nets"};
	log_assert(handle.kind() != ysta::Handle::Kind::Term);
	std::string name = handle.swigName();
	Tcl_Interp *interp = yosys_get_tcl_interp();
	Tcl_CmdInfo info;
	// make the swig command of the handle
	if (!Tcl_GetCommandInfo(interp, name.c_str(), &info)) {
		std::string object_name = objectName(the_sta->sdcNetwork(), handle);
		std::string getter = getters[int(handle.kind())] + std::string(" -quiet ") + tclQuoted(object_name);
		Tcl_EvalEx(interp, getter.c_str(), -1, TCL_EVAL_GLOBAL);
	}
	return name;
}

Handles objectsFor(const SdcObjectRef &ref, Use use, Pins pins, const Handles &stand_ins, const Handles &found)
{
	bool exact = pins == Pins::Exact;
	bool through = use == Use::Through && !exact;
	if (through && !ref.result.old_scope_pins.empty())
		return handlesOf(ref.result.old_scope_pins);
	if (exact && !ref.result.own_pins.empty())
		return handlesOf(ref.result.own_pins);
	if (!stand_ins.empty())
		return stand_ins;
	return found;
}

std::string refHandles(const SdcObjectRef &ref, const Handles &objects)
{
	std::string handles;
	for (ysta::Handle handle : objects) {
		if (ref.kind == SdcObjectRef::Kind::TopPort)
			handles += " " + portByName(objectName(&staNetwork(), handle));
		else
			handles += " " + swigHandle(handle);
	}
	return handles;
}

std::string renderWord(const SdcWord &word, RenderState &render)
{
	std::string edges = disabledEdges(word, render);
	if (!edges.empty())
		return edges;
	if (word.kind == WordKind::Getter)
		return renderGetter(word, word.use, Pins::ByUse, render);
	std::string clock = clockGetter(word, render);
	if (!clock.empty())
		return clock;
	if (isEdgePair(word.nested_words)) {
		SdcWords pair = {word.nested_words[2], word.nested_words[4]};
		return "[::ysta::edges " + renderWords(pair, render) + "]";
	}
	if (word.kind == WordKind::Substitution)
		return "[" + renderWords(word.nested_words, render) + "]";
	return word.text;
}

std::string renderWords(const SdcWords &words, RenderState &render)
{
	std::string text;
	for (const SdcWord &word : words)
		appendSpaced(text, renderWord(word, render));
	return text;
}

std::string renderGetter(const SdcWord &word, Use use, Pins pins, RenderState &render)
{
	std::string handles;
	bool only_barrier_instance_pins = true;
	for (SdcObjectRef *ref : word.refs) {
		Handles found = ref->objects();
		Handles stand_ins = standIns(*ref, use, found);
		if (stand_ins.empty())
			stand_ins = renamedCells(*ref, render.words_after_from);
		if (!stand_ins.empty())
			render.on_stand_ins.insert(ref);
		render.result.refs_rendered = true;
		render.result.found |= !found.empty();
		if (found.empty() && !ref->origin.on_barrier_instance && render.missing_refs.insert(ref).second)
			appendSpaced(render.result.missing, ref->name);
		Handles objects = objectsFor(*ref, use, pins, stand_ins, found);
		noteBarrierPinNames(*ref, objects);
		handles += refHandles(*ref, objects);
		only_barrier_instance_pins &= ref->origin.on_barrier_instance;
	}
	render.result.empty_getter |= handles.empty() && !only_barrier_instance_pins;
	return "[list" + handles + "]";
}

// {a b} -> a b, "a b" -> a b, anything else unchanged
std::string unwrapped(const std::string &text, char open, char close)
{
	if (text.size() >= 2 && text.front() == open && text.back() == close)
		return text.substr(1, text.size() - 2);
	return text;
}

} // namespace opensta_net
