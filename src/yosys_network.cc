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
	return std::tie(kind, wire, index) < std::tie(other.kind, other.wire, other.index);
}

detail::NameClaim detail::netClaim(const Yosys::NetView &netlist, Netlist::Net *net)
{
	if (net->constant != Netlist::Net::Const::None)
		return {net, ClaimKind::Canonical, "", int(net->constant)};
	Netlist::NetName name = netlist.wireName(net);
	return {net, ClaimKind::Canonical, name.wire, name.index};
}

void detail::addClaim(NetNames &names, const std::string &name, const NameClaim &claim)
{
	auto [it, fresh] = names.try_emplace(name, claim);
	if (!fresh && claim < it->second)
		it->second = claim;
}

// Canonical names, then each public wire bit by source path and by host name
size_t detail::scopeNetNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, NetNames &names)
{
	size_t shared = 0;
	for (Netlist::Net *net : netlist.nets(scope)) {
		std::string name = detail::staNetName(netlist, net);
		shared += names.count(name);
		detail::addClaim(names, name, detail::netClaim(netlist, net));
	}
	for (const Netlist::NetAlias &alias : netlist.aliases(scope)) {
		NameClaim claim = {alias.net, ClaimKind::Alias, alias.name.wire, alias.name.index};
		Netlist::NetName host = alias.name;
		host.hdlpath.clear();
		detail::addClaim(names, detail::staWireName(alias.name), claim);
		detail::addClaim(names, detail::staWireName(host), claim);
	}
	return shared;
}

std::vector<std::string> detail::childNames(const Netlist::Instance *scope, std::vector<std::string> &own)
{
	std::vector<std::string> names;
	for (const Netlist::Instance *child : scope->children) {
		names.push_back(detail::staInstName(child));
		own.push_back(detail::staName(child->name));
	}
	for (bool changed = true; changed;) {
		changed = false;
		std::map<std::string, int> counts;
		for (const std::string &name : names)
			counts[name]++;
		for (size_t i = 0; i < names.size(); i++) {
			if (counts[names[i]] > 1 && names[i] != own[i]) {
				names[i] = own[i];
				changed = true;
			}
		}
	}
	return names;
}

void detail::scopeChildNames(const Netlist::Instance *scope, const std::vector<std::string> &names,
                             const std::vector<std::string> &own, ChildNames &children)
{
	for (size_t i = 0; i < names.size(); i++) {
		Netlist::Instance *child = scope->children[i];
		children[names[i]] = child;
		if (own[i] != names[i])
			children.emplace(own[i], child);
	}
}

void YosysNetwork::checkOwner(const YInstance *inst) const
{
	if (inst != nullptr && inst->netlist != view_)
		report_->error(2931, "a handle of an earlier network names no object, get the object again.");
}

const YInstance *YosysNetwork::asInst(const sta::Instance *inst) const
{
	const YInstance *record = reinterpret_cast<const YInstance *>(inst);
	checkOwner(record);
	return record;
}

const YPin *YosysNetwork::asPin(const sta::Pin *pin) const
{
	const YPin *record = reinterpret_cast<const YPin *>(pin);
	if (record != nullptr)
		checkOwner(record->inst);
	return record;
}

const YNet *YosysNetwork::asNet(const sta::Net *net) const
{
	const YNet *record = reinterpret_cast<const YNet *>(net);
	if (record != nullptr)
		checkOwner(record->scope);
	return record;
}

const YTerm *YosysNetwork::asTerm(const sta::Term *term) const
{
	const YTerm *record = reinterpret_cast<const YTerm *>(term);
	if (record != nullptr)
		checkOwner(record->pin->inst);
	return record;
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

size_t YosysNetwork::takeSharedNames()
{
	return std::exchange(shared_name_count_, 0);
}

void YosysNetwork::setEpochPhase(EpochPhase phase)
{
	epoch_phase_ = phase;
}

bool YosysNetwork::epochClosed() const
{
	return epoch_phase_ == EpochPhase::Closed;
}

void YosysNetwork::setOpenEpochHook(void (*hook)())
{
	open_epoch_hook_ = hook;
}

bool YosysNetwork::isLeaf(const sta::Pin *pin) const
{
	return token_sink_ != nullptr || sta::Network::isLeaf(pin);
}

void YosysNetwork::setTokenSink(std::vector<Token> *sink)
{
	token_sink_ = sink;
	token_index_.clear();
}

// one stable token per object, write_sdc sorts by name
std::string YosysNetwork::token(Token::ObjectKind kind, const void *object) const
{
	auto [it, fresh] = token_index_.try_emplace(object, token_sink_->size());
	if (fresh)
		token_sink_->push_back({kind, object});
	return std::string(detail::token_prefix) + std::to_string(it->second);
}

std::string YosysNetwork::pathName(const sta::Instance *instance) const
{
	if (token_sink_ != nullptr && instance != topInstance())
		return token(Token::ObjectKind::Cell, instance);
	return sta::Network::pathName(instance);
}

std::string YosysNetwork::pathName(const sta::Pin *pin) const
{
	if (token_sink_ != nullptr)
		return token(Token::ObjectKind::Pin, pin);
	return sta::Network::pathName(pin);
}

// write_sdc writes top ports by name
std::string YosysNetwork::name(const sta::Port *port) const
{
	if (token_sink_ != nullptr && cell(port) == reinterpret_cast<sta::Cell *>(top_cell_))
		return token(Token::ObjectKind::Port, port);
	return sta::ConcreteNetwork::name(port);
}

std::string YosysNetwork::pathName(const sta::Net *net) const
{
	if (token_sink_ != nullptr)
		return token(Token::ObjectKind::Net, net);
	return sta::Network::pathName(net);
}

bool YosysNetwork::isLinked() const
{
	open_epoch_hook_();
	return linked() && epoch_phase_ == EpochPhase::Open && view_->valid();
}

void YosysNetwork::attach(Yosys::NetView *view)
{
	view_ = view;
	// Named as OpenSTA's verilog reader library
	if (design_lib_ == nullptr)
		design_lib_ = makeLibrary("verilog", "");
}

void YosysNetwork::link()
{
	YInstance *top = view_->top();
	top_cell_ = makeShapedCell(top->master, StaCellKind::Hierarchical, *top->ports);
	inst_cells_[top->id] = top_cell_;
	inst_names_[top->id] = top->name;
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
	inst_cells_.clear();
	pin_ports_.clear();
	child_names_.clear();
	inst_names_.clear();
	net_names_.clear();
	pin_names_.clear();
	master_uses_.clear();
	scope_cells_.clear();
	clearNetDrvrPinMap();
}

bool YosysNetwork::ports(const YInstance &inst, std::vector<Netlist::PortShape> &out)
{
	sta::LibertyCell *lcell = findLibertyCell(inst.type.c_str());
	if (lcell == nullptr && inst.kind == Netlist::Instance::Kind::InternalLeaf)
		report_->error(2923, "internal cell type {} has no liberty cell, map the design first "
		                     "(techmap/dfflibmap/abc, or icell_liberty).", inst.type);
	if (lcell == nullptr)
		return false;
	sta::CellPortIterator *port_iter = portIterator(cell(lcell));
	while (port_iter->hasNext()) {
		sta::Port *port = port_iter->next();
		Netlist::PortShape &shape = out.emplace_back();
		shape.name = name(port);
		shape.width = size(port);
		shape.vector = hasMembers(port);
		if (shape.vector) {
			shape.from = fromIndex(port);
			shape.to = toIndex(port);
		}
		sta::PortDirection *dir = direction(port);
		shape.dir = Netlist::portDir(dir->isAnyInput(), dir->isAnyOutput());
	}
	delete port_iter;
	return true;
}

bool YosysNetwork::isRegister(const YInstance &inst)
{
	const sta::LibertyCell *cell = libertyCell(reinterpret_cast<const sta::Instance *>(&inst));
	return cell != nullptr && cell->isSequential();
}

bool YosysNetwork::isClockPin(const YPin &pin)
{
	const sta::LibertyPort *port = libertyPort(reinterpret_cast<const sta::Pin *>(&pin));
	return port != nullptr && port->isClock();
}

bool YosysNetwork::isBuffer(const YInstance &inst)
{
	const sta::LibertyCell *cell = libertyCell(reinterpret_cast<const sta::Instance *>(&inst));
	return cell != nullptr && cell->isBuffer();
}

void YosysNetwork::buildTables(YInstance *scope)
{
	std::vector<std::string> own;
	std::vector<std::string> names = detail::childNames(scope, own);
	for (size_t i = 0; i < names.size(); i++)
		inst_names_[scope->children[i]->id] = names[i];
	detail::scopeChildNames(scope, names, own, child_names_[scope->id]);
	for (YInstance *child : scope->children) {
		if (!child->isLeaf()) {
			buildTables(child);
			inst_cells_[child->id] = scopeCell(child);
		}
		indexPins(child);
	}
	shared_name_count_ += detail::scopeNetNames(*view_, scope, net_names_[scope->id]);
}

sta::ConcreteCell *YosysNetwork::scopeCell(const YInstance *scope)
{
	std::string key = scope->master;
	if (master_uses_[scope->master] > 1)
		key += "\n" + scopeBody(scope);
	auto [it, fresh] = scope_cells_.try_emplace(key, nullptr);
	if (fresh)
		it->second = makeShapedCell(freeCellName(scope), StaCellKind::Hierarchical, *scope->ports);
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
		lines.push_back("T " + detail::pinKey(*view_, pin) + " " + detail::netKey(*view_, inner));
	}
	for (const YNet *net : view_->nets(scope))
		lines.push_back("N " + detail::staNetName(*view_, net));
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
	std::string child_name = instName(child);
	lines.push_back("C " + child_name + " " + name(reinterpret_cast<sta::Cell *>(inst_cells_.get(child->id))));
	for (const YPin *pin : child->pins) {
		std::string net = detail::netKey(*view_, pin->net);
		lines.push_back("c " + child_name + " " + detail::pinKey(*view_, pin) + " " + net);
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
		if (!child->isLeaf()) {
			master_uses_[child->master]++;
			countMasters(child);
		}
	}
}

sta::ConcretePort *YosysNetwork::portOf(const YPin *pin) const
{
	sta::ConcreteCell *ccell = inst_cells_.get(pin->inst->id);
	const std::string &raw = view_->shape(pin).name;
	std::string port_name = raw;
	if (ccell->libertyCell() == nullptr)
		port_name = detail::staName(raw);
	sta::Port *port = reinterpret_cast<sta::Port *>(ccell->findPort(port_name));
	if (port == nullptr)
		report_->error(2903, "port {} missing on {}.", raw, name(reinterpret_cast<sta::Cell *>(ccell)));
	if (!hasMembers(port))
		return reinterpret_cast<sta::ConcretePort *>(port);
	sta::Port *bit = findBusBit(port, view_->hdlIndex(pin));
	if (bit == nullptr)
		report_->error(2916, "bus bit {} out of range on port {}.", pin->bit, name(port));
	return reinterpret_cast<sta::ConcretePort *>(bit);
}

// Cell, pin ports and port-bit names of an instance
void YosysNetwork::indexPins(YInstance *inst)
{
	if (inst->isLeaf())
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
	if (lcell != nullptr)
		ccell = reinterpret_cast<sta::ConcreteCell *>(cell(lcell));
	else
		ccell = makeShapedCell(inst.type, StaCellKind::Leaf, *inst.ports);
	type_cells_[inst.type] = ccell;
	return ccell;
}

sta::ConcreteCell *YosysNetwork::makeShapedCell(const std::string &cell_name, StaCellKind kind, const std::vector<Netlist::PortShape> &shapes)
{
	sta::Cell *scell = makeCell(design_lib_, cell_name, kind == StaCellKind::Leaf, "");
	for (const Netlist::PortShape &shape : shapes) {
		std::string port_name = detail::staName(shape.name);
		sta::Port *port;
		if (shape.scalar())
			port = makePort(scell, port_name);
		else
			port = makeBusPort(scell, port_name, shape.from, shape.to);
		setDirection(port, detail::staDirection(shape.dir));
	}
	return reinterpret_cast<sta::ConcreteCell *>(scell);
}

sta::Instance *YosysNetwork::topInstance() const
{
	if (!linked())
		return nullptr;
	return reinterpret_cast<sta::Instance *>(view_->top());
}

std::string YosysNetwork::name(const sta::Instance *instance) const
{
	return instName(asInst(instance));
}

const std::string &YosysNetwork::instName(const YInstance *inst) const
{
	return inst_names_.get(inst->id);
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
	return asInst(instance)->isLeaf();
}

sta::Instance *YosysNetwork::findChild(const sta::Instance *parent, std::string_view name) const
{
	return reinterpret_cast<sta::Instance *>(detail::lookup(child_names_.get(asInst(parent)->id), name));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, std::string_view port_name) const
{
	return reinterpret_cast<sta::Pin *>(detail::lookup(pin_names_.get(asInst(instance)->id), port_name));
}

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
		if (name == instName(child))
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
		if (claim.kind == detail::ClaimKind::Canonical)
			nets.push_back(claim.net);
	}
	return detail::castIterator<sta::Net *>(nets);
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
	return detail::staNetName(*view_, asNet(net));
}

ObjectId YosysNetwork::id(const sta::Net *net) const
{
	if (net == nullptr)
		return sta::object_id_null;
	return asNet(net)->id;
}

sta::Net *YosysNetwork::findNet(const sta::Instance *instance, std::string_view net_name) const
{
	return reinterpret_cast<sta::Net *>(detail::lookup(net_names_.get(asInst(instance)->id), net_name).net);
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
		if (pattern->match(name) && seen.insert(claim.net).second)
			matches.push_back(reinterpret_cast<sta::Net *>(claim.net));
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
	for (YNet *net : view_->constNets(Netlist::Net::Const::Zero))
		zero_nets.insert(reinterpret_cast<sta::Net *>(net));
	for (YNet *net : view_->constNets(Netlist::Net::Const::One))
		one_nets.insert(reinterpret_cast<sta::Net *>(net));
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
