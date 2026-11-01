// before each pass or rtlil edit the epoch ends:
// 1. write_sdc writes the constraints, objects as tokens
// 2. each token becomes a ref, which anchors its rtlil objects
// 3. passes carry the anchors
// 4. the first query after a change opens the epoch: rebuild, then resolveAndApply
// the trace keeps what write_sdc does not write and the commands since the epoch end (::ysta::record in trace.tcl)
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

bool writeAllModes(SdcTextByMode &result)
{
	for (const sta::Mode *mode : the_sta->modes()) {
		if (!writeModeSdc(mode->name(), result[mode->name()]))
			return false;
	}
	return true;
}

// writes, parses and anchors the constraints, a failed write_sdc keeps the last constraints and the journal
bool captureEpochConstraints()
{
	SdcTextByMode modes;
	the_sta->tokens.clear();
	the_sta->yosys_network->setTokenSink(&the_sta->tokens);
	bool written = writeAllModes(modes);
	const char *list = "journal";
	if (written)
		list = "kept";
	int untexted = appendTraceTexts(modes, list);
	the_sta->yosys_network->setTokenSink(nullptr);
	if (written)
		the_sta->constraints = {};
	bool journaled = written || untexted == 0;
	tclEval("::ysta::clear_journal");
	parseCommands(modes);
	orderOverrides(the_sta->constraints.commands);
	the_sta->tracker.checkpoint(the_sta->view, the_sta->constraints.view_refs, the_sta->track_mode);
	return journaled;
}

void closeEpoch()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (the_sta->running || !network->linked() || network->epoch_closed)
		return;
	the_sta->view.recheck();
	if (!the_sta->view.upToDate())
		log_debug("opensta_net: the view is stale, the anchors of the last epoch end stay.\n");
	else if (!captureEpochConstraints())
		log_warning("opensta_net: write_sdc failed and the journal lacks commands since the last epoch end: they are lost.\n");
	network->epoch_closed = true;
}

void openEpoch()
{
	bool closed = the_sta->yosys_network->epoch_closed || the_sta->rebuild_pending;
	if (closed && yosys_design != nullptr)
		syncView(yosys_design);
}

bool needsSync()
{
	return the_sta->rebuild_pending || (the_sta->yosys_network->linked() && !the_sta->view.upToDate());
}

bool canBuild(Design *design)
{
	std::string reason;
	if (NetView::buildable(design, reason)) {
		the_sta->last_unbuildable_reason.clear();
		return true;
	}
	if (reason == the_sta->last_unbuildable_reason)
		return false;
	the_sta->last_unbuildable_reason = reason;
	log_warning("opensta_net: cannot rebuild the STA network: %s. It stays unlinked until a sync succeeds.\n",
	            reason.c_str());
	return false;
}

void syncView(Design *design)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	the_sta->view.recheck();
	if (the_sta->running || (!network->linked() && !the_sta->rebuild_pending))
		return;
	if (!needsSync()) {
		network->epoch_closed = false;
		return;
	}
	if (!canBuild(design))
		return;
	SyncRunning running(the_sta->running);
	network->epoch_closed = false;
	rebuildAndReapply(design);
}

SyncRunning::SyncRunning(bool &flag) : running(flag)
{
	running = true;
}

SyncRunning::~SyncRunning()
{
	running = false;
}

void rebuildAndReapply(Design *design)
{
	warnDroppedParasitics();
	// the commands since the epoch end read no network object, else a sync ran before them
	SdcTextByMode journal;
	int untexted = appendTraceTexts(journal, "journal");
	std::string error;
	if (!linkView(design, error)) {
		the_sta->rebuild_pending = true;
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync succeeds.\n", error.c_str());
		return;
	}
	the_sta->rebuild_pending = false;
	// the apply keeps the unwritten commands again
	tclEval("::ysta::clear");
	parseCommands(journal);
	int lost = untexted + resolveAndApply();
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

// before the pins of this view go, in the mode of each disable
void undoHierPinDisables()
{
	sta::Scene *cmd_scene = the_sta->cmdScene();
	std::string cmd_mode = the_sta->cmdMode()->name();
	if (tclInt("::ysta::undo_hier_disables") != 1)
		return;
	the_sta->setCmdScene(cmd_scene);
	the_sta->setCmdMode(cmd_mode);
}

void dropView()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (network->linked()) {
		undoHierPinDisables();
		the_sta->networkChanged();
		network->unlink();
	}
	the_sta->view.clear();
}

bool linkView(Design *design, std::string &error)
{
	dropView();
	auto t0 = std::chrono::steady_clock::now();
	ysta::YosysNetwork *network = the_sta->yosys_network;
	NetView *view = &the_sta->view;
	network->attach(view);
	try {
		pool<const Cell *> barriers = the_sta->tracker.barrierInstances(design);
		view->build(design, {network, &barriers});
		network->link();
	} catch (const sta::Exception &exception) {
		network->unlink();
		view->clear();
		error = exception.what();
		return false;
	}
	warnSharedNames();
	long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	auto [insts, nets, pins] = view->counts();
	log("opensta_net: built network: %zu instances, %zu nets, %zu pins (%ld ms).\n", insts, nets, pins, ms);
	return true;
}

void linkDesign(Design *design)
{
	std::string reason;
	if (!NetView::buildable(design, reason))
		log_cmd_error("opensta_net: cannot link: %s.\n", reason.c_str());
	the_sta->constraints = {};
	the_sta->tracker.clear(design);
	tclEval("::ysta::clear");
	the_sta->yosys_network->epoch_closed = false;
	if (!linkView(design, reason))
		log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
}

} // namespace opensta_net
