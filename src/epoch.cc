// before each pass or rtlil edit the epoch ends:
// 1. write_sdc writes the constraints, objects as tokens
// 2. each token becomes a ref, which anchors its rtlil objects
// 3. passes carry the anchors
// 4. the first query after a change opens the epoch: rebuild, then applyConstraints
#include "opensta_net.h"

#include <fstream>

#include "sta/MinMax.hh"
#include "sta/Mode.hh"
#include "sta/Parasitics.hh"
#include "sta/Scene.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

bool writeModeSdc(const std::string &mode, std::string &text)
{
	std::string file = make_temp_file(get_base_tmpdir() + "/opensta_net_XXXXXX.sdc");
	bool ok = tclEval("write_sdc -no_timestamp -mode {" + mode + "} {" + file + "}");
	if (ok) {
		std::stringstream stream;
		stream << std::ifstream(file).rdbuf();
		text = stream.str();
	} else {
		log_warning("opensta_net: write_sdc failed: %s\n", Tcl_GetStringResult(yosys_get_tcl_interp()));
	}
	remove(file.c_str());
	return ok;
}

bool writeAllModes(ModeTexts &result)
{
	for (const sta::Mode *mode : the_sta->modes()) {
		if (!writeModeSdc(mode->name(), result[mode->name()]))
			return false;
	}
	return true;
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

void closeEpoch()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (the_sta->syncing || !network->linked() || network->locked())
		return;
	the_sta->view->invalidateCheck();
	if (!the_sta->view->valid())
		log_debug("opensta_net: the view is stale, the anchors of the last epoch end stay.\n");
	else if (!injectConstraints())
		log_warning("opensta_net: write_sdc failed and the journal lacks commands since the last epoch end: they are lost.\n");
	network->setLocked(true);
}

void openEpoch()
{
	bool closed = the_sta->yosys_network->locked() || the_sta->rebuild_pending;
	if (closed && yosys_design != nullptr)
		syncView(yosys_design);
}

bool needsSync()
{
	return the_sta->rebuild_pending || (the_sta->yosys_network->linked() && !the_sta->view->valid());
}

bool canBuild(Design *design)
{
	std::string reason;
	if (NetView::buildable(design, reason)) {
		the_sta->unbuildable_reason.clear();
		return true;
	}
	if (reason == the_sta->unbuildable_reason)
		return false;
	the_sta->unbuildable_reason = reason;
	log_warning("opensta_net: cannot rebuild the STA network: %s. It stays unlinked until a sync succeeds.\n",
	            reason.c_str());
	return false;
}

void syncView(Design *design)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	the_sta->view->invalidateCheck();
	if (the_sta->syncing || (!network->linked() && !the_sta->rebuild_pending))
		return;
	if (!needsSync()) {
		network->setLocked(false);
		return;
	}
	if (!canBuild(design))
		return;
	the_sta->syncing = true;
	network->setLocked(false);
	rebuild(design);
	the_sta->syncing = false;
}

void rebuild(Design *design)
{
	warnDroppedParasitics();
	std::string error;
	if (!linkView(design, error)) {
		the_sta->rebuild_pending = true;
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync succeeds.\n",
		            error.c_str());
		return;
	}
	the_sta->rebuild_pending = false;
	tclEval("::ysta::clear");
	int lost = applyConstraints();
	log("opensta_net: synced after design change, %d constraint(s) lost.\n", lost);
}

void warnDroppedParasitics()
{
	for (const sta::Scene *scene : the_sta->scenes()) {
		for (const sta::MinMax *min_max : sta::MinMax::range()) {
			sta::Parasitics *parasitics = scene->parasitics(min_max);
			if (parasitics != nullptr && parasitics->haveParasitics()) {
				log_warning("opensta_net: the rebuild drops the loaded parasitics, read them again.\n");
				return;
			}
		}
	}
}

void warnSharedNames()
{
	size_t shared = the_sta->yosys_network->takeSharedNames();
	if (shared > 0)
		log_warning("opensta_net: %zu net(s) have the name of another net in their scope. A getter by that name "
		            "finds the other net.\n", shared);
}

void dropView()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (network->linked()) {
		the_sta->networkChanged();
		network->unlink();
	}
	if (the_sta->view->built())
		the_sta->retired_views.push_back(std::exchange(the_sta->view, the_sta->view->retire()));
}

bool linkView(Design *design, std::string &error)
{
	dropView();
	auto t0 = std::chrono::steady_clock::now();
	ysta::YosysNetwork *network = the_sta->yosys_network;
	network->attach(the_sta->view.get());
	try {
		the_sta->view->build(design, network);
		network->link();
	} catch (const sta::Exception &exception) {
		network->unlink();
		the_sta->view->reset();
		error = exception.what();
		return false;
	}
	warnSharedNames();
	long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	auto [insts, nets, pins] = the_sta->view->counts();
	log("opensta_net: built network: %zu instances, %zu nets, %zu pins (%ld ms).\n", insts, nets, pins, ms);
	return true;
}

void linkDesign(Design *design)
{
	std::string reason;
	if (!NetView::buildable(design, reason))
		log_cmd_error("opensta_net: cannot link: %s.\n", reason.c_str());
	the_sta->constraints = {};
	the_sta->view->clearAnchors(design);
	tclEval("::ysta::clear");
	the_sta->yosys_network->setLocked(false);
	if (!linkView(design, reason))
		log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
}

} // namespace opensta_net
