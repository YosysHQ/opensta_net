// SPDX-License-Identifier: GPL-3.0-or-later
#include "yosys_network.hh"

#include "sta/Liberty.hh"
#include "sta/PatternMatch.hh"
#include "sta/PortDirection.hh"
#include "sta/Report.hh"

namespace ysta {

using sta::ObjectId;

namespace detail {

// Iterator over a copied sequence
template <typename T> class SeqIterator : public sta::Iterator<T> {
public:
	explicit SeqIterator(std::vector<T> seq) : seq_(std::move(seq)) {}
	bool hasNext() override { return index_ < seq_.size(); }
	T next() override { return seq_[index_++]; }

private:
	std::vector<T> seq_;
	size_t index_ = 0;
};

template <typename T, typename Record> sta::Iterator<T> *castIterator(const std::vector<Record *> &records)
{
	std::vector<T> seq;
	for (Record *record : records)
		seq.push_back(reinterpret_cast<T>(record));
	return new detail::SeqIterator<T>(std::move(seq));
}

} // namespace detail

// '/', '[', ']' and '\' escaped, as OpenSTA's readers store names: SdcNetwork
// depends on it to resolve esc/inst or a[3]
std::string detail::staName(std::string_view raw)
{
	std::string result;
	result.reserve(raw.size());
	for (char c : raw) {
		if (c == '/' || c == '[' || c == ']' || c == '\\')
			result += '\\';
		result += c;
	}
	return result;
}

std::string detail::staUnescape(std::string_view name)
{
	std::string result;
	result.reserve(name.size());
	for (size_t i = 0; i < name.size(); i++) {
		if (name[i] == '\\' && i + 1 < name.size())
			i++;
		result += name[i];
	}
	return result;
}

// {"u1", "r"} -> u1\/r, as the reference reader stores the flat identifier \u1/r
std::string detail::staPath(const std::vector<std::string> &path)
{
	std::string result;
	for (const std::string &part : path) {
		if (!result.empty())
			result += "\\/";
		result += detail::staName(part);
	}
	return result;
}

std::string detail::staInstName(const Netlist::Instance *inst)
{
	if (inst->hdlpath.empty())
		return detail::staName(inst->name);
	return detail::staPath(inst->hdlpath);
}

std::string detail::staWireName(const Netlist::NetName &name)
{
	std::string base;
	if (name.hdlpath.empty())
		base = detail::staName(name.wire);
	else
		base = detail::staPath(name.hdlpath);
	if (name.scalar)
		return base;
	return base + "[" + std::to_string(name.index) + "]";
}

std::string detail::staNetName(const Yosys::NetView &netlist, const Netlist::Net *net)
{
	// VerilogReader names
	if (net->constant == Netlist::Net::Const::One)
		return "one_";
	if (net->constant == Netlist::Net::Const::Zero)
		return "zero_";
	return detail::staWireName(netlist.wireName(net));
}

sta::PortDirection *detail::staDirection(Netlist::Dir dir)
{
	switch (dir) {
	case Netlist::Dir::Input: return sta::PortDirection::input();
	case Netlist::Dir::Output: return sta::PortDirection::output();
	case Netlist::Dir::Inout: return sta::PortDirection::bidirect();
	default: return sta::PortDirection::unknown();
	}
}

void detail::reversePortRuns(std::vector<Netlist::Pin *> &pins)
{
	size_t begin = 0;
	while (begin < pins.size()) {
		size_t end = begin + 1;
		while (end < pins.size() && pins[end]->inst == pins[begin]->inst && pins[end]->port == pins[begin]->port)
			end++;
		std::reverse(pins.begin() + begin, pins.begin() + end);
		begin = end;
	}
}

// port[bit]
std::string detail::pinKey(const Yosys::NetView &netlist, const Netlist::Pin *pin)
{
	return netlist.shape(pin).name + "[" + std::to_string(netlist.hdlIndex(pin)) + "]";
}

std::string detail::netKey(const Yosys::NetView &netlist, const Netlist::Net *net)
{
	if (net == nullptr)
		return "-";
	return detail::staNetName(netlist, net);
}

bool detail::NameClaim::operator<(const NameClaim &other) const
{
	return std::tie(alias, wire, index) < std::tie(other.alias, other.wire, other.index);
}

detail::NameClaim detail::netClaim(const Yosys::NetView &netlist, Netlist::Net *net)
{
	if (net->constant != Netlist::Net::Const::None)
		return {net, false, "", int(net->constant)};
	Netlist::NetName name = netlist.wireName(net);
	return {net, false, name.wire, name.index};
}

void detail::addClaim(NetNames &names, const std::string &name, const NameClaim &claim)
{
	auto [it, fresh] = names.try_emplace(name, claim);
	if (!fresh && claim < it->second)
		it->second = claim;
}

// Canonical names, then each public wire bit by source path and by host
// name
void detail::scopeNetNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, NetNames &names)
{
	for (Netlist::Net *net : netlist.nets(scope))
		detail::addClaim(names, detail::staNetName(netlist, net), detail::netClaim(netlist, net));
	for (const Netlist::Alias &alias : netlist.aliases(scope)) {
		NameClaim claim = {alias.net, true, alias.name.wire, alias.name.index};
		Netlist::NetName host = alias.name;
		host.hdlpath.clear();
		detail::addClaim(names, detail::staWireName(alias.name), claim);
		detail::addClaim(names, detail::staWireName(host), claim);
	}
}

void detail::scopeChildNames(const Netlist::Instance *scope, ChildNames &children)
{
	for (Netlist::Instance *child : scope->children) {
		children[detail::staInstName(child)] = child;
		children.emplace(detail::staName(child->name), child);
	}
}

const YInstance *YosysNetwork::asInst(const sta::Instance *inst)
{
	return reinterpret_cast<const YInstance *>(inst);
}

const YPin *YosysNetwork::asPin(const sta::Pin *pin)
{
	return reinterpret_cast<const YPin *>(pin);
}

const YNet *YosysNetwork::asNet(const sta::Net *net)
{
	return reinterpret_cast<const YNet *>(net);
}

const YTerm *YosysNetwork::asTerm(const sta::Term *term)
{
	return reinterpret_cast<const YTerm *>(term);
}

bool YosysNetwork::linkNetwork(std::string_view, bool, sta::Report *report)
{
	report->warn(2915, "the network is managed by yosys, link_design only cleared "
	                   "constraints and timing. Use `opensta_net -link` to rebuild.");
	return true;
}

bool YosysNetwork::linked() const
{
	return top_cell_ != nullptr;
}

std::vector<std::string> YosysNetwork::takeStubTypes()
{
	return std::move(stub_types_);
}

void YosysNetwork::setReadStale(bool on)
{
	read_stale_ = on;
}

void YosysNetwork::setLocked(bool on)
{
	locked_ = on;
}

bool YosysNetwork::locked() const
{
	return locked_;
}

void YosysNetwork::setBeforeCheck(void (*hook)())
{
	before_check_ = hook;
}

bool YosysNetwork::isLinked() const
{
	before_check_();
	return linked() && !locked_ && (read_stale_ || netlist_->valid());
}

void YosysNetwork::attach(Yosys::NetView *netlist)
{
	netlist_ = netlist;
	// Named as OpenSTA's verilog reader library
	if (design_lib_ == nullptr)
		design_lib_ = makeLibrary("verilog", "");
}

void YosysNetwork::link()
{
	YInstance *top = netlist_->top();
	top_cell_ = makeShapedCell(top->master, false, *top->ports);
	inst_cells_[top->id] = top_cell_;
	indexPins(top);
	countMasters(top);
	buildTables(top);
	master_uses_.clear();
	scope_cells_.clear();
	if (pin_ports_.data.size() >= 0x40000000)
		report_->error(2927, "link: too many pins for 30-bit object ids.");
}

void YosysNetwork::unlink()
{
	// Stub and scope cells die with the library
	if (design_lib_ != nullptr)
		deleteLibrary(design_lib_);
	design_lib_ = nullptr;
	top_cell_ = nullptr;
	type_cells_.clear();
	cell_ports_.clear();
	inst_cells_.clear();
	pin_ports_.clear();
	child_names_.clear();
	net_names_.clear();
	pin_names_.clear();
	master_uses_.clear();
	scope_cells_.clear();
	recording_ = false;
	recorded_.clear();
	scopes_.clear();
	clearNetDrvrPinMap();
}

bool YosysNetwork::ports(const YInstance &inst, std::vector<Netlist::PortShape> &out)
{
	sta::CellPortIterator *port_iter = portIterator(reinterpret_cast<sta::Cell *>(typeCell(inst)));
	while (port_iter->hasNext()) {
		sta::Port *port = port_iter->next();
		Netlist::PortShape &shape = out.emplace_back();
		// Stub port names are escaped, liberty names are raw
		shape.name = detail::staUnescape(name(port));
		shape.width = size(port);
		if (hasMembers(port)) {
			shape.from = fromIndex(port);
			shape.to = toIndex(port);
		}
		sta::PortDirection *dir = direction(port);
		shape.dir = Netlist::portDir(dir->isAnyInput(), dir->isAnyOutput());
	}
	delete port_iter;
	return true;
}

void YosysNetwork::buildTables(YInstance *scope)
{
	detail::scopeChildNames(scope, child_names_[scope->id]);
	for (YInstance *child : scope->children) {
		if (!child->leaf) {
			buildTables(child);
			inst_cells_[child->id] = scopeCell(child);
		}
		indexPins(child);
	}
	detail::scopeNetNames(*netlist_, scope, net_names_[scope->id]);
	scopes_.push_back(scope);
}

sta::ConcreteCell *YosysNetwork::scopeCell(const YInstance *scope)
{
	std::string key = scope->master;
	if (master_uses_[scope->master] > 1)
		key += "\n" + scopeBody(scope);
	auto [it, fresh] = scope_cells_.try_emplace(key, nullptr);
	if (fresh)
		it->second = makeShapedCell(freeCellName(scope), false, *scope->ports);
	return it->second;
}

std::string YosysNetwork::scopeBody(const YInstance *scope) const
{
	std::vector<std::string> lines;
	for (const Netlist::PortShape &shape : *scope->ports)
		lines.push_back(std::format("P {} {} {} {} {}", shape.name, shape.width, shape.from, shape.to, int(shape.dir)));
	for (const YPin *pin : scope->pins) {
		const YNet *inner = nullptr;
		if (pin->term != nullptr)
			inner = pin->term->net;
		lines.push_back("T " + detail::pinKey(*netlist_, pin) + " " + detail::netKey(*netlist_, inner));
	}
	for (const YNet *net : netlist_->nets(scope))
		lines.push_back("N " + detail::staNetName(*netlist_, net));
	for (const YInstance *child : scope->children)
		childLines(child, lines);
	std::sort(lines.begin(), lines.end());
	std::string body;
	for (const std::string &line : lines)
		body += line + "\n";
	return body;
}

// Name, cell, and the net on each pin
void YosysNetwork::childLines(const YInstance *child, std::vector<std::string> &lines) const
{
	std::string child_name = detail::staInstName(child);
	lines.push_back("C " + child_name + " " + name(reinterpret_cast<sta::Cell *>(inst_cells_.get(child->id))));
	for (const YPin *pin : child->pins) {
		std::string net = detail::netKey(*netlist_, pin->net);
		lines.push_back("c " + child_name + " " + detail::pinKey(*netlist_, pin) + " " + net);
	}
}

std::string YosysNetwork::freeCellName(const YInstance *scope) const
{
	if (findCell(design_lib_, scope->master) == nullptr)
		return scope->master;
	std::string name = scope->type;
	for (int n = 1; findCell(design_lib_, name) != nullptr; n++)
		name = scope->type + "_" + std::to_string(n);
	return name;
}

void YosysNetwork::countMasters(const YInstance *scope)
{
	for (const YInstance *child : scope->children) {
		if (!child->leaf) {
			master_uses_[child->master]++;
			countMasters(child);
		}
	}
}

sta::ConcretePort *YosysNetwork::portOf(const YPin *pin) const
{
	sta::ConcreteCell *ccell = inst_cells_.get(pin->inst->id);
	const std::string &raw = netlist_->shape(pin).name;
	sta::Port *port = reinterpret_cast<sta::Port *>(detail::lookup(detail::lookup(cell_ports_, ccell), raw));
	if (port == nullptr)
		report_->error(2903, "port {} missing on {}.", raw, name(reinterpret_cast<sta::Cell *>(ccell)));
	if (!hasMembers(port))
		return reinterpret_cast<sta::ConcretePort *>(port);
	sta::Port *bit = findBusBit(port, netlist_->hdlIndex(pin));
	if (bit == nullptr)
		report_->error(2916, "bus bit {} out of range on port {}.", pin->bit, name(port));
	return reinterpret_cast<sta::ConcretePort *>(bit);
}

// Cell, pin ports and port-bit names of an instance
void YosysNetwork::indexPins(YInstance *inst)
{
	if (inst->leaf)
		inst_cells_[inst->id] = typeCell(*inst);
	detail::PinNames &names = pin_names_[inst->id];
	for (YPin *pin : inst->pins) {
		sta::ConcretePort *cport = portOf(pin);
		pin_ports_[pin->id] = cport;
		names[name(reinterpret_cast<sta::Port *>(cport))] = pin;
	}
}

sta::ConcreteCell *YosysNetwork::typeCell(const YInstance &inst)
{
	auto it = type_cells_.find(inst.type);
	if (it != type_cells_.end())
		return it->second;
	sta::LibertyCell *lcell = findLibertyCell(inst.type.c_str());
	sta::ConcreteCell *ccell;
	if (lcell != nullptr) {
		ccell = reinterpret_cast<sta::ConcreteCell *>(cell(lcell));
		indexPorts(ccell);
	} else {
		ccell = makeStubCell(inst);
	}
	type_cells_[inst.type] = ccell;
	return ccell;
}

sta::ConcreteCell *YosysNetwork::makeStubCell(const YInstance &inst)
{
	if (inst.internal)
		report_->error(2923, "internal cell type {} has no liberty cell, map the design first "
		                     "(techmap/dfflibmap/abc, or icell_liberty).", inst.type);
	// A blackbox module's ports, when known
	std::vector<Netlist::PortShape> shapes;
	netlist_->hostPorts(&inst, shapes);
	stub_types_.push_back(inst.type);
	return makeShapedCell(inst.type, true, shapes);
}

sta::ConcreteCell *YosysNetwork::makeShapedCell(const std::string &cell_name, bool leaf, const std::vector<Netlist::PortShape> &shapes)
{
	sta::Cell *scell = makeCell(design_lib_, cell_name, leaf, "");
	for (const Netlist::PortShape &shape : shapes) {
		std::string port_name = detail::staName(shape.name);
		sta::Port *port;
		if (shape.width == 1)
			port = makePort(scell, port_name);
		else
			port = makeBusPort(scell, port_name, shape.from, shape.to);
		setDirection(port, detail::staDirection(shape.dir));
	}
	sta::ConcreteCell *ccell = reinterpret_cast<sta::ConcreteCell *>(scell);
	indexPorts(ccell);
	return ccell;
}

void YosysNetwork::indexPorts(sta::ConcreteCell *ccell)
{
	detail::PortNames &ports = cell_ports_[ccell];
	sta::CellPortIterator *port_iter = portIterator(reinterpret_cast<sta::Cell *>(ccell));
	while (port_iter->hasNext()) {
		sta::Port *port = port_iter->next();
		ports[detail::staUnescape(name(port))] = reinterpret_cast<sta::ConcretePort *>(port);
	}
	delete port_iter;
}

void YosysNetwork::beginRecording()
{
	recording_ = true;
	recorded_.clear();
}

std::set<YInstance *> YosysNetwork::endRecording()
{
	recording_ = false;
	return std::move(recorded_);
}

bool YosysNetwork::recording() const
{
	return recording_;
}

void YosysNetwork::recordScope(YInstance *scope) const
{
	for (; scope != nullptr && !netlist_->isTop(scope); scope = scope->parent) {
		// Ancestors already recorded
		if (!recorded_.insert(scope).second)
			return;
	}
}

void YosysNetwork::recordInstance(const sta::Instance *instance) const
{
	if (!recording_ || instance == nullptr)
		return;
	YInstance *inst = const_cast<YInstance *>(asInst(instance));
	if (inst->leaf)
		recordScope(inst->parent);
	else
		recordScope(inst);
}

void YosysNetwork::recordPin(const sta::Pin *pin) const
{
	if (!recording_ || pin == nullptr)
		return;
	const YPin *ypin = asPin(pin);
	recordInstance(reinterpret_cast<const sta::Instance *>(ypin->inst));
	const YNet *net = ypin->net;
	if (net == nullptr && ypin->term != nullptr)
		net = ypin->term->net;
	recordNet(net);
}

void YosysNetwork::recordNet(const YNet *net) const
{
	if (recording_ && net != nullptr)
		recordScope(net->scope);
}

sta::Instance *YosysNetwork::topInstance() const
{
	if (!linked())
		return nullptr;
	return reinterpret_cast<sta::Instance *>(netlist_->top());
}

std::string YosysNetwork::name(const sta::Instance *instance) const
{
	const YInstance *inst = asInst(instance);
	// The top keeps its plain name, as does OpenSTA's reader
	if (netlist_->isTop(inst))
		return inst->name;
	return detail::staInstName(inst);
}

ObjectId YosysNetwork::id(const sta::Instance *instance) const
{
	return asInst(instance)->id;
}

std::string YosysNetwork::getAttribute(const sta::Instance *, std::string_view) const
{
	return "";
}

const sta::AttributeMap &YosysNetwork::attributeMap(const sta::Instance *) const
{
	static const sta::AttributeMap empty;
	return empty;
}

sta::Cell *YosysNetwork::cell(const sta::Instance *instance) const
{
	return reinterpret_cast<sta::Cell *>(inst_cells_.get(asInst(instance)->id));
}

sta::Instance *YosysNetwork::parent(const sta::Instance *instance) const
{
	return reinterpret_cast<sta::Instance *>(asInst(instance)->parent);
}

bool YosysNetwork::isLeaf(const sta::Instance *instance) const
{
	return asInst(instance)->leaf;
}

sta::Instance *YosysNetwork::findChild(const sta::Instance *parent, std::string_view name) const
{
	YInstance *child = detail::lookup(child_names_.get(asInst(parent)->id), name);
	sta::Instance *instance = reinterpret_cast<sta::Instance *>(child);
	recordInstance(instance);
	return instance;
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, std::string_view port_name) const
{
	sta::Pin *pin = reinterpret_cast<sta::Pin *>(detail::lookup(pin_names_.get(asInst(instance)->id), port_name));
	recordPin(pin);
	return pin;
}

// By port object: how STA walks instances, so not recorded
sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, const sta::Port *port) const
{
	return reinterpret_cast<sta::Pin *>(detail::lookup(pin_names_.get(asInst(instance)->id), name(port)));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, const sta::LibertyPort *port) const
{
	return reinterpret_cast<sta::Pin *>(detail::lookup(pin_names_.get(asInst(instance)->id), port->name()));
}

// Name order, host-name aliases skipped
sta::InstanceChildIterator *YosysNetwork::childIterator(const sta::Instance *instance) const
{
	std::vector<YInstance *> children;
	for (const auto &[name, child] : child_names_.get(asInst(instance)->id)) {
		if (name == detail::staInstName(child))
			children.push_back(child);
	}
	return detail::castIterator<sta::Instance *>(children);
}

sta::InstancePinIterator *YosysNetwork::pinIterator(const sta::Instance *instance) const
{
	std::vector<YPin *> pins = asInst(instance)->pins;
	detail::reversePortRuns(pins);
	return detail::castIterator<sta::Pin *>(pins);
}

// Name order, aliases skipped
sta::InstanceNetIterator *YosysNetwork::netIterator(const sta::Instance *instance) const
{
	std::vector<YNet *> nets;
	for (const auto &[name, claim] : net_names_.get(asInst(instance)->id)) {
		if (!claim.alias)
			nets.push_back(claim.net);
	}
	return detail::castIterator<sta::Net *>(nets);
}

sta::InstanceSeq YosysNetwork::findInstancesMatching(const sta::Instance *context, const sta::PatternMatch *pattern) const
{
	sta::InstanceSeq matches = sta::Network::findInstancesMatching(context, pattern);
	for (const sta::Instance *instance : matches)
		recordInstance(instance);
	return matches;
}

sta::InstanceSeq YosysNetwork::findInstancesHierMatching(const sta::Instance *instance, const sta::PatternMatch *pattern) const
{
	sta::InstanceSeq matches = sta::Network::findInstancesHierMatching(instance, pattern);
	for (const sta::Instance *match : matches)
		recordInstance(match);
	return matches;
}

sta::PinSeq YosysNetwork::findPinsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern) const
{
	sta::PinSeq matches = sta::Network::findPinsMatching(instance, pattern);
	for (const sta::Pin *pin : matches)
		recordPin(pin);
	return matches;
}

sta::PinSeq YosysNetwork::findPinsHierMatching(const sta::Instance *instance, const sta::PatternMatch *pattern) const
{
	sta::PinSeq matches = sta::Network::findPinsHierMatching(instance, pattern);
	for (const sta::Pin *pin : matches)
		recordPin(pin);
	return matches;
}

ObjectId YosysNetwork::id(const sta::Pin *pin) const
{
	return asPin(pin)->id;
}

sta::Instance *YosysNetwork::instance(const sta::Pin *pin) const
{
	return reinterpret_cast<sta::Instance *>(asPin(pin)->inst);
}

sta::Net *YosysNetwork::net(const sta::Pin *pin) const
{
	return reinterpret_cast<sta::Net *>(asPin(pin)->net);
}

sta::Term *YosysNetwork::term(const sta::Pin *pin) const
{
	return reinterpret_cast<sta::Term *>(asPin(pin)->term);
}

sta::Port *YosysNetwork::port(const sta::Pin *pin) const
{
	return reinterpret_cast<sta::Port *>(pin_ports_.get(asPin(pin)->id));
}

sta::PortDirection *YosysNetwork::direction(const sta::Pin *pin) const
{
	return direction(port(pin));
}

sta::VertexId YosysNetwork::vertexId(const sta::Pin *pin) const
{
	return asPin(pin)->user;
}

void YosysNetwork::setVertexId(sta::Pin *pin, sta::VertexId id)
{
	asPin(pin)->user = id;
}

ObjectId YosysNetwork::id(const sta::Term *term) const
{
	return asTerm(term)->id;
}

sta::Net *YosysNetwork::net(const sta::Term *term) const
{
	return reinterpret_cast<sta::Net *>(asTerm(term)->net);
}

sta::Pin *YosysNetwork::pin(const sta::Term *term) const
{
	return reinterpret_cast<sta::Pin *>(asTerm(term)->pin);
}

std::string YosysNetwork::name(const sta::Net *net) const
{
	return detail::staNetName(*netlist_, asNet(net));
}

ObjectId YosysNetwork::id(const sta::Net *net) const
{
	return asNet(net)->id;
}

sta::Net *YosysNetwork::findNet(const sta::Instance *instance, std::string_view net_name) const
{
	YNet *net = detail::lookup(net_names_.get(asInst(instance)->id), net_name).net;
	recordNet(net);
	return reinterpret_cast<sta::Net *>(net);
}

void YosysNetwork::findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern, sta::NetSeq &matches) const
{
	if (!pattern->hasWildcards()) {
		if (sta::Net *net = findNet(instance, pattern->pattern()); net != nullptr)
			matches.push_back(net);
		return;
	}
	std::set<const YNet *> seen;
	for (const auto &[name, claim] : net_names_.get(asInst(instance)->id)) {
		if (pattern->match(name) && seen.insert(claim.net).second) {
			recordNet(claim.net);
			matches.push_back(reinterpret_cast<sta::Net *>(claim.net));
		}
	}
}

sta::Instance *YosysNetwork::instance(const sta::Net *net) const
{
	return reinterpret_cast<sta::Instance *>(asNet(net)->scope);
}

// ConcreteNet prepends each connected pin and term
sta::NetPinIterator *YosysNetwork::pinIterator(const sta::Net *net) const
{
	const std::vector<YPin *> &pins = asNet(net)->pins;
	std::vector<YPin *> seq(pins.rbegin(), pins.rend());
	detail::reversePortRuns(seq);
	return detail::castIterator<const sta::Pin *>(seq);
}

sta::NetTermIterator *YosysNetwork::termIterator(const sta::Net *net) const
{
	const std::vector<YTerm *> &terms = asNet(net)->terms;
	return detail::castIterator<sta::Term *>(std::vector<YTerm *>(terms.rbegin(), terms.rend()));
}

bool YosysNetwork::isPower(const sta::Net *net) const
{
	return asNet(net)->constant == Netlist::Net::Const::One;
}

bool YosysNetwork::isGround(const sta::Net *net) const
{
	return asNet(net)->constant == Netlist::Net::Const::Zero;
}

sta::ConstantPinIterator *YosysNetwork::constantPinIterator()
{
	sta::NetSet zero_nets(this);
	sta::NetSet one_nets(this);
	for (YInstance *scope : scopes_) {
		if (YNet *net = netlist_->constNet(scope, false); net != nullptr)
			zero_nets.insert(reinterpret_cast<sta::Net *>(net));
		if (YNet *net = netlist_->constNet(scope, true); net != nullptr)
			one_nets.insert(reinterpret_cast<sta::Net *>(net));
	}
	return new sta::NetworkConstantPinIterator(this, zero_nets, one_nets);
}

void YosysNetwork::visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const
{
	sta::Network::visitConnectedPins(net, visitor, visited_nets);
}

void YosysNetwork::refuseEdit(const char *what) const
{
	report_->error(2929, "{}: network edits are not supported, edit the design in yosys.", what);
}

void YosysNetwork::setAttribute(sta::Instance *, std::string_view, std::string_view)
{
	refuseEdit("set_attribute");
}

void YosysNetwork::readNetlistBefore()
{
	refuseEdit("read_verilog");
}

void YosysNetwork::clear()
{
	refuseEdit("clear_network");
}

} // namespace ysta
