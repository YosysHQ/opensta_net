// sync, link and capture of the STA network, see opensta_net.h
#include "opensta_net.h"

#include "sta/Mode.hh"
#include "sta/Parasitics.hh"
#include "sta/Search.hh"

USING_YOSYS_NAMESPACE

namespace opensta_net {

// write_sdc with object tokens, each anchors its rtlil objects
void Epochs::capture()
{
	if (!staNetwork().linked())
		return;
	ysta::TokenIndexes tokens;
	std::string capture_modes = "::ysta::capture";
	for (const sta::Mode *mode : the_sta->modes())
		capture_modes += " " + tclQuoted(mode->name());
	// token mode only here
	staNetwork().setTokens(&tokens);
	bool written = tclEval(capture_modes);
	staNetwork().setTokens(nullptr);
	if (!written) {
		log_warning("opensta_net: write_sdc failed (%s), the last capture stays, a rebuild before the next capture "
		            "drops the newer changes.\n", Tcl_GetStringResult(the_sta->tclInterp()));
		return;
	}
	makeRefs(tokens);
	the_sta->tracker.checkpoint(staNetwork().view(), trackedRefs());
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

// a failed build or an unbuildable design leaves no network, the next call builds again, throws -> stop
void Epochs::sync(Design *design)
{
	bool is_current = staNetwork().linked() && staNetwork().view().upToDate();
	if (!started_ || is_current)
		return;
	try {
		rebuild(design);
	} catch (...) {
		stop();
		throw;
	}
}

// no network until -link
void Epochs::stop()
{
	started_ = false;
	dropView();
}

void Epochs::rebuild(Design *design)
{
	warnDroppedParasitics();
	dropView();
	if (!canBuild(design))
		return;
	std::string error;
	if (!linkView(design, error)) {
		log_warning("opensta_net: cannot rebuild the STA network (%s), it stays unlinked until a sync "
		            "succeeds.\n", error.c_str());
		return;
	}
	int lost = resolveAndApply();
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

void dropView()
{
	if (staNetwork().linked()) {
		// Sdc::clear keeps hierarchical pin disables, undo them while the pins live
		tclEval("::ysta::undo_hier_disables");
		// Sta::clear leaves the generated clock data of the old graph
		the_sta->search()->arrivalsInvalid();
		the_sta->networkChanged();
		// Sta::clear frees the clocks and edges
		tclEval("array unset ::ysta::live_handles");
		// from now on old nets exist, see write_sdc_cmd
		tclEval("incr ::ysta::rebuilds");
		staNetwork().unlink();
	}
}

// a log_cmd_error in the build is a failed build
bool linkView(Design *design, std::string &error)
{
	dropView();
	ysta::YosysNetwork *network = &staNetwork();
	size_t shared_names = 0;
	try {
		auto guard = logger().error_throw_scope();
		shared_names = network->link(std::make_unique<NetView>(design, network));
	} catch (const sta::Exception &exception) {
		network->unlink();
		error = exception.what();
		return false;
	} catch (const log_cmd_error_exception &) {
		error = "the NetView build failed, see the error above";
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

// an unbuildable design keeps the old network
// a failed build is a command error, it ends in stop(), so no call retries it until -link
void Epochs::link(Design *design)
{
	std::string reason;
	if (!NetView::buildable(design, reason))
		log_cmd_error("opensta_net: cannot link: %s.\n", reason.c_str());
	the_sta->refs.clear();
	the_sta->last_unbuildable_reason.clear();
	tclEval("::ysta::clear");
	started_ = true;
	try {
		if (!linkView(design, reason))
			log_cmd_error("opensta_net: cannot link: %s\n", reason.c_str());
	} catch (...) {
		stop();
		throw;
	}
}

// for the netlist write, barriers and keeps go, the network and the constraints end here until -link
void Epochs::unprotect(Design *design)
{
	stop();
	the_sta->tracker.unprotect(design);
}

} // namespace opensta_net
