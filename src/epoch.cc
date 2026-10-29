// before each pass or rtlil edit the epoch ends:
// 1. write_sdc writes the constraints, objects as tokens
// 2. each token becomes a ref, which anchors its rtlil objects
// 3. passes carry the anchors
// 4. the first query after a change opens the epoch: rebuild, then resolveAndApply
// the trace keeps what write_sdc does not write and the commands since the epoch end (::ysta::record in trace.tcl)
#include "opensta_net.h"

#include <fstream>

#include "sta/Liberty.hh"
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
	YosysSta::EpochState &epoch = the_sta->epoch;
	SdcTextByMode modes;
	epoch.tokens.clear();
	the_sta->yosys_network->setTokenSink(&epoch.tokens);
	bool written = writeAllModes(modes);
	const char *list = "journal";
	if (written)
		list = "kept";
	int untexted = appendTraceTexts(modes, list);
	the_sta->yosys_network->setTokenSink(nullptr);
	if (written)
		epoch.constraints = {};
	bool journaled = written || untexted == 0;
	tclEval("::ysta::clear_journal");
	parseCommands(modes);
	orderOverrides(epoch.constraints.commands);
	epoch.tracker.checkpoint(*the_sta->views.view, epoch.constraints.view_refs, epoch.track_mode);
	return journaled;
}

void closeEpoch()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (the_sta->sync.running || !network->linked() || network->epochClosed())
		return;
	the_sta->views.view->recheck();
	if (!the_sta->views.view->upToDate())
		log_debug("opensta_net: the view is stale, the anchors of the last epoch end stay.\n");
	else if (!captureEpochConstraints())
		log_warning("opensta_net: write_sdc failed and the journal lacks commands since the last epoch end: they are lost.\n");
	network->setEpochPhase(ysta::EpochPhase::Closed);
}

void openEpoch()
{
	bool closed = the_sta->yosys_network->epochClosed() || the_sta->sync.rebuild_pending;
	if (closed && yosys_design != nullptr)
		syncView(yosys_design);
}

bool needsSync()
{
	return the_sta->sync.rebuild_pending || (the_sta->yosys_network->linked() && !the_sta->views.view->upToDate());
}

bool canBuild(Design *design)
{
	YosysSta::SyncState &sync = the_sta->sync;
	std::string reason;
	if (NetView::buildable(design, reason)) {
		sync.last_unbuildable_reason.clear();
		return true;
	}
	if (reason == sync.last_unbuildable_reason)
		return false;
	sync.last_unbuildable_reason = reason;
	log_warning("opensta_net: cannot rebuild the STA network: %s. It stays unlinked until a sync succeeds.\n",
	            reason.c_str());
	return false;
}

void syncView(Design *design)
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	YosysSta::SyncState &sync = the_sta->sync;
	the_sta->views.view->recheck();
	if (sync.running || (!network->linked() && !sync.rebuild_pending))
		return;
	if (!needsSync()) {
		network->setEpochPhase(ysta::EpochPhase::Open);
		return;
	}
	if (!canBuild(design))
		return;
	SyncRunning running(sync.running);
	network->setEpochPhase(ysta::EpochPhase::Open);
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
		the_sta->sync.rebuild_pending = true;
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync succeeds.\n",
		            error.c_str());
		return;
	}
	the_sta->sync.rebuild_pending = false;
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

void StaleSdc::clear(sta::Sdc *sdc)
{
	(sdc->*&StaleSdc::net_res_map_).clear();
	(sdc->*&StaleSdc::net_voltage_map_).clear();
	(sdc->*&StaleSdc::disabled_wire_edges_).clear();
}

void StaleProperties::keepOnly(sta::Properties &properties, const std::vector<const void *> &objects)
{
	auto &values = properties.*&StaleProperties::prop_values_;
	const auto &types = properties.*&StaleProperties::prop_types_;
	std::map<sta::PropertyKey, sta::PropertyValue> kept;
	for (const auto &[type, value_type] : types) {
		for (const void *object : objects) {
			auto it = values.find(sta::PropertyKey(object, type.second));
			if (it != values.end())
				kept.insert(*it);
		}
	}
	values = std::move(kept);
}

// the objects that a relink keeps, a liberty object is also its network object
std::vector<const void *> lastingObjects()
{
	std::vector<const void *> objects;
	for (const sta::Mode *mode : the_sta->modes())
		objects.push_back(mode);
	for (const sta::Scene *scene : the_sta->scenes())
		objects.push_back(scene);
	sta::LibertyLibraryIterator *libraries = the_sta->network()->libertyLibraryIterator();
	while (libraries->hasNext())
		addLiberty(libraries->next(), objects);
	delete libraries;
	return objects;
}

void addLiberty(const sta::LibertyLibrary *library, std::vector<const void *> &objects)
{
	objects.push_back(library);
	sta::LibertyCellIterator cells(library);
	while (cells.hasNext()) {
		const sta::LibertyCell *cell = cells.next();
		objects.push_back(cell);
		sta::LibertyCellPortIterator ports(cell);
		while (ports.hasNext())
			objects.push_back(ports.next());
		sta::LibertyCellPortBitIterator bits(cell);
		while (bits.hasNext())
			objects.push_back(bits.next());
	}
}

void dropView()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	YosysSta::ViewOwner &views = the_sta->views;
	if (network->linked()) {
		the_sta->networkChanged();
		for (sta::Mode *mode : the_sta->modes())
			StaleSdc::clear(mode->sdc());
		StaleProperties::keepOnly(the_sta->properties(), lastingObjects());
		network->unlink();
	}
	if (!views.view->built())
		return;
	views.view->retire();
	views.retired_views.push_back(std::exchange(views.view, std::make_unique<NetView>()));
}

bool linkView(Design *design, std::string &error)
{
	dropView();
	auto t0 = std::chrono::steady_clock::now();
	ysta::YosysNetwork *network = the_sta->yosys_network;
	NetView *view = the_sta->views.view.get();
	network->attach(view);
	try {
		pool<const Cell *> barriers = the_sta->epoch.tracker.barrierInstances(design);
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
	the_sta->epoch.constraints = {};
	the_sta->epoch.tracker.clear(design);
	tclEval("::ysta::clear");
	the_sta->yosys_network->setEpochPhase(ysta::EpochPhase::Open);
	if (!linkView(design, reason))
		log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
}

} // namespace opensta_net
