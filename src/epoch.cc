// epoch end, sync and link of the STA network, see opensta_net.h
#include "opensta_net.h"

#include "sta/Mode.hh"
#include "sta/Parasitics.hh"
#include "sta/Search.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

void Epochs::capture()
{
	std::map<ysta::Handle, size_t> tokens;
	std::string capture_modes = "::ysta::capture";
	for (const sta::Mode *mode : the_sta->modes())
		capture_modes += " " + tclQuoted(mode->name());
	// the only token mode
	staNetwork().setTokens(&tokens);
	bool written = tclEval(capture_modes);
	staNetwork().setTokens(nullptr);
	if (!written) {
		log_warning("opensta_net: write_sdc failed: %s\n", Tcl_GetStringResult(yosys_get_tcl_interp()));
		return;
	}
	makeRefs(tokens);
	the_sta->tracker.checkpoint(staNetwork().view(), trackedRefs(), the_sta->track_barriers);
}

void Epochs::end()
{
	if (phase_ != Phase::Open)
		return;
	// a stale view cannot anchor its objects in the changed design, the sync then runs the journal again by name
	if (staNetwork().view().upToDate())
		capture();
	phase_ = Phase::Ended;
}

bool Epochs::stale() const
{
	return phase_ != Phase::Stopped && (!staNetwork().linked() || !staNetwork().view().upToDate());
}

bool Epochs::syncForQuery(Design *design)
{
	if (phase_ == Phase::Building)
		return staNetwork().linked();
	if (phase_ == Phase::Ended && design != nullptr)
		sync(design);
	return phase_ == Phase::Open;
}

// warns when the reason changes
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
	// before the link, the old pins still live
	tclEval("::ysta::name_journal");
	tclEval("::ysta::undo_hier_disables");
	std::string error;
	if (!linkView(design, error)) {
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync "
		            "succeeds.\n", error.c_str());
		return Phase::Ended;
	}
	int lost = resolveAndApply();
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

void dropView()
{
	if (staNetwork().linked()) {
		// Sta::clear leaves the generated clock data of the old graph
		the_sta->search()->arrivalsInvalid();
		the_sta->networkChanged();
		staNetwork().unlink();
	}
}

// a log_cmd_error in the build throws in a yosys command, else yosys exits
bool linkView(Design *design, std::string &error)
{
	dropView();
	ysta::YosysNetwork *network = &staNetwork();
	size_t shared_names = 0;
	try {
		shared_names = network->link(std::make_unique<NetView>(design, network));
	} catch (const sta::Exception &exception) {
		network->unlink();
		error = exception.what();
		return false;
	}
	if (shared_names > 0)
		log_warning("opensta_net: %zu wire(s) have the name of a constant net (one_ or zero_). A getter by that "
		            "name finds the constant net.\n", shared_names);
	const NetView &view = network->view();
	log("opensta_net: built network: %zu instances, %zu nets, %zu pins.\n", view.instances().size() - 1,
	    view.nets().size(), view.pins().size());
	return true;
}

// a failed build is a command error, it ends in Stopped, so no query retries it
void Epochs::link(Design *design)
{
	std::string reason;
	if (!NetView::buildable(design, reason))
		log_cmd_error("opensta_net: cannot link: %s.\n", reason.c_str());
	the_sta->refs.clear();
	the_sta->track_barriers = true;
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

// sync through the barriers -> capture puts only ids -> remove barriers -> sync by the ids of cells and wires
// only an Open epoch has the capture, Stopped also skips the syncs
void Epochs::unprotect(Design *design)
{
	sync(design);
	the_sta->track_barriers = false;
	end();
	// optbarriers runs as a pass, its beforePass calls end(), a no-op on the ended epoch
	the_sta->tracker.unprotect(design);
	sync(design);
}

} // namespace opensta_net
