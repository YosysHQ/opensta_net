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

// a failed write leaves no text of any mode
bool writeAllModes(SdcTextByMode &result)
{
	for (const sta::Mode *mode : the_sta->modes()) {
		if (!writeModeSdc(mode->name(), result[mode->name()])) {
			result.clear();
			return false;
		}
	}
	return true;
}

// returns false when we lost commands since the last capture
// write_sdc ok -> constraints are its text plus the kept list
// write_sdc failed -> the journal joins the old constraints
bool Epochs::capture()
{
	SdcTextByMode modes;
	std::vector<ysta::Token> tokens;
	// the only token mode, for write_sdc and the trace texts
	// the parse below names objects by name
	std::optional<ysta::TokenScope> token_mode(std::in_place, staNetwork(), tokens);
	bool written = writeAllModes(modes);
	const char *list = "journal";
	if (written)
		list = "kept";
	int untexted = appendTraceTexts(modes, list);
	token_mode.reset();
	if (written)
		the_sta->constraints = {};
	bool journaled = written || untexted == 0;
	tclEval("::ysta::clear_journal");
	parseCommands(modes, tokens, the_sta->constraints);
	// tightest path exception last, it wins on a shared path
	// flatten can make two exceptions share a path
	orderOverrides(the_sta->constraints.commands);
	the_sta->tracker.checkpoint(staNetwork().view(), the_sta->constraints.view_refs, the_sta->track_barriers);
	return journaled;
}

void Epochs::end()
{
	if (phase_ != Phase::Open)
		return;
	// a stale view cannot anchor its objects in the changed design
	// the journal waits for the next capture or sync
	if (!staNetwork().view().upToDate())
		log_debug("opensta_net: the view is stale, the anchors of the last epoch end stay.\n");
	else if (!capture())
		log_warning("opensta_net: write_sdc failed and the journal lacks commands since the last epoch end: "
		            "they are lost.\n");
	phase_ = Phase::Ended;
}

bool Epochs::syncForQuery(Design *design)
{
	if (phase_ == Phase::Building)
		return staNetwork().linked();
	if ((phase_ == Phase::Ended || phase_ == Phase::BuildFailed) && design != nullptr)
		sync(design);
	return phase_ == Phase::Open;
}

bool Epochs::stale() const
{
	return phase_ != Phase::Stopped && (!staNetwork().linked() || !staNetwork().view().upToDate());
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

void Epochs::sync(Design *design)
{
	if (phase_ == Phase::Stopped || phase_ == Phase::Building)
		return;
	if (!stale()) {
		phase_ = Phase::Open;
		return;
	}
	// queries see no network until a sync succeeds
	if (!canBuild(design)) {
		if (phase_ == Phase::Open)
			phase_ = Phase::Ended;
		return;
	}
	phase_ = Phase::Building;
	try {
		phase_ = rebuild(design);
	} catch (...) {
		phase_ = Phase::Stopped;
		dropView();
		throw;
	}
}

Epochs::Phase Epochs::rebuild(Design *design)
{
	warnDroppedParasitics();
	// read before the link, getters sync first so the journal ran none
	// it names clocks, library objects and held handles by plain name
	// it parses with no tokens
	SdcTextByMode journal;
	int untexted = appendTraceTexts(journal, "journal");
	std::string error;
	if (!linkView(design, error)) {
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync "
		            "succeeds.\n", error.c_str());
		return Phase::BuildFailed;
	}
	// the apply keeps the unwritten commands again
	tclEval("::ysta::clear");
	// the journal joins the captured constraints
	// resolveAndApply finds their objects by the anchors and runs them
	parseCommands(journal, {}, the_sta->constraints);
	int lost = untexted + resolveAndApply();
	log("opensta_net: synced after design change, %d constraint(s) lost.\n", lost);
	return Phase::Open;
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

void warnSharedNames(size_t shared)
{
	if (shared > 0)
		log_warning("opensta_net: %zu net(s) have the name of another net in their scope. A getter by that "
		            "name finds the other net.\n", shared);
}

void dropView()
{
	ysta::YosysNetwork *network = the_sta->yosys_network;
	if (network->linked()) {
		// before the pins of this view go, in the mode of each disable
		tclEval("::ysta::undo_hier_disables");
		the_sta->networkChanged();
		network->unlink();
	}
	the_sta->user_names.clear();
}

bool linkView(Design *design, std::string &error)
{
	dropView();
	auto t0 = std::chrono::steady_clock::now();
	ysta::YosysNetwork *network = the_sta->yosys_network;
	size_t shared_names = 0;
	try {
		pool<const Cell *> barriers = the_sta->tracker.barrierInstances(design);
		NetView::BuildOptions options = {network, &barriers};
		shared_names = network->link(std::make_unique<NetView>(design, options));
	} catch (const sta::Exception &exception) {
		network->unlink();
		error = exception.what();
		return false;
	}
	warnSharedNames(shared_names);
	long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	auto [insts, nets, pins] = network->view().counts();
	log("opensta_net: built network: %zu instances, %zu nets, %zu pins (%ld ms).\n", insts, nets, pins, ms);
	return true;
}

// a failed build is a command error and ends in Stopped
// it never ends in BuildFailed, so no query retries it
void Epochs::link(Design *design)
{
	std::string reason;
	if (!NetView::buildable(design, reason))
		log_cmd_error("opensta_net: cannot link: %s.\n", reason.c_str());
	the_sta->constraints = {};
	the_sta->last_unbuildable_reason.clear();
	the_sta->tracker.clear(design);
	tclEval("::ysta::clear");
	phase_ = Phase::Building;
	try {
		if (!linkView(design, reason))
			log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
	} catch (...) {
		phase_ = Phase::Stopped;
		dropView();
		throw;
	}
	phase_ = Phase::Open;
}

// capture without barriers -> remove barriers -> sync by anchors only
// BuildFailed and Stopped skip the capture, Stopped also skips the sync
void Epochs::unprotect(Design *design)
{
	the_sta->track_barriers = false;
	// Ended opens again for the capture
	if (phase_ == Phase::Ended)
		phase_ = Phase::Open;
	end();
	// runs optbarriers as a pass
	// its beforePass calls end(), a no-op on the ended epoch
	the_sta->tracker.unprotect(design);
	sync(design);
}

} // namespace opensta_net
