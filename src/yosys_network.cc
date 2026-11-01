// SPDX-License-Identifier: GPL-3.0-or-later
#include "yosys_network.hh"

#include <algorithm>

#include "sta/Liberty.hh"
#include "sta/PatternMatch.hh"
#include "sta/PortDirection.hh"
#include "sta/Report.hh"

namespace ysta {

using sta::ObjectId;

static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "a handle holds a kind, a generation and a record id");

Handle::Handle(Kind kind, uint32_t generation, uint32_t id)
{
	uint64_t high = 1u << 31 | uint32_t(kind) << 29 | (generation & generation_mask);
	bits = high << 32 | id;
}

Handle::Kind Handle::kind() const
{
	return Kind(bits >> 61 & 3);
}

uint32_t Handle::generation() const
{
	return bits >> 32 & generation_mask;
}

uint32_t Handle::id() const
{
	return uint32_t(bits);
}

// little-endian hex, then the type: _40a3..._p_Pin
std::string Handle::swigName() const
{
	static const char *types[] = {"Instance", "Pin", "Net", "Term"};
	std::string name = "_";
	for (int byte = 0; byte < 8; byte++)
		name += Yosys::stringf("%02x", unsigned(bits >> (8 * byte) & 0xff));
	return name + "_p_" + types[int(kind())];
}

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
	return detail::staWireName(netlist.name(net));
}

sta::PortDirection *detail::staDirection(Netlist::PortDir dir)
{
	switch (dir) {
	case Netlist::PortDir::Input: return sta::PortDirection::input();
	case Netlist::PortDir::Output: return sta::PortDirection::output();
	case Netlist::PortDir::Inout: return sta::PortDirection::bidirect();
	default: return sta::PortDirection::unknown();
	}
}

void detail::reversePortRuns(std::vector<Netlist::Pin *> &pins)
{
	size_t begin = 0;
	while (begin < pins.size()) {
		size_t end = begin + 1;
		while (end < pins.size() && pins[end]->inst == pins[begin]->inst && pins[end]->port_index == pins[begin]->port_index)
			end++;
		std::reverse(pins.begin() + begin, pins.begin() + end);
		begin = end;
	}
}

// The first claim of a name owns it until a claim beats it (a net name beats an alias, then the smaller wire bit wins)
void detail::claimName(RankedClaims &claims, const Netlist::NetName &name, Netlist::Net *net, bool canonical)
{
	RankedClaim ranked = {{net, canonical}, name.wire, name.index};
	auto [it, added] = claims.try_emplace(detail::staWireName(name), ranked);
	RankedClaim &owner = it->second;
	if (added)
		return;
	if (canonical != owner.claim.canonical) {
		if (canonical)
			owner = ranked;
		return;
	}
	if (std::tie(ranked.wire, ranked.index) < std::tie(owner.wire, owner.index))
		owner = ranked;
}

size_t detail::scopeNetNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, NetNames &names)
{
	RankedClaims claims;
	size_t shared = 0;
	for (Netlist::Net *net : netlist.nets(scope)) {
		if (net->constant != Netlist::Net::Const::None)
			continue;
		Netlist::NetName name = netlist.name(net);
		shared += claims.count(detail::staWireName(name));
		detail::claimName(claims, name, net, true);
	}
	// an alias also by its wire name
	for (Netlist::Net *net : netlist.nets(scope)) {
		for (Netlist::NetName alias : netlist.aliases(net)) {
			detail::claimName(claims, alias, net, false);
			alias.hdlpath.clear();
			detail::claimName(claims, alias, net, false);
		}
	}
	for (const auto &[sta_name, ranked] : claims)
		names[sta_name] = ranked.claim;
	for (Netlist::Net *net : netlist.nets(scope)) {
		if (net->constant == Netlist::Net::Const::None)
			continue;
		NameClaim &claim = names[detail::staNetName(netlist, net)];
		if (claim.canonical)
			shared++;
		claim = {net, true};
	}
	return shared;
}

void YosysNetwork::refuseHandle() const
{
	report_->error(2931, "a handle of an earlier network names no object, get the object again.");
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

void YosysNetwork::setStaleNames(bool on)
{
	stale_names_ = on;
}

bool YosysNetwork::staleNet(const sta::Net *net) const
{
	return stale_names_ && net != nullptr && Handle(net).generation() != generation_;
}

// one stable token per object, write_sdc sorts by name
std::string YosysNetwork::token(Token::Kind kind, Handle handle) const
{
	auto [it, fresh] = token_index_.try_emplace({kind, handle}, token_sink_->size());
	if (fresh)
		token_sink_->push_back({kind, handle});
	return std::string(detail::token_prefix) + std::to_string(it->second);
}

std::string YosysNetwork::pathName(const sta::Instance *instance) const
{
	if (token_sink_ != nullptr && instance != topInstance())
		return token(Token::Kind::Instance, instance);
	return sta::Network::pathName(instance);
}

std::string YosysNetwork::pathName(const sta::Pin *pin) const
{
	if (token_sink_ != nullptr)
		return token(Token::Kind::Pin, pin);
	return sta::Network::pathName(pin);
}

// write_sdc writes top ports by name
std::string YosysNetwork::name(const sta::Port *port) const
{
	sta::Pin *top_pin = nullptr;
	if (token_sink_ != nullptr && cell(port) == reinterpret_cast<sta::Cell *>(top_cell_))
		top_pin = findPin(topInstance(), sta::ConcreteNetwork::name(port));
	// a bus port has no pin, it stays a name
	if (top_pin != nullptr)
		return token(Token::Kind::TopPort, top_pin);
	return sta::ConcreteNetwork::name(port);
}

std::string YosysNetwork::pathName(const sta::Net *net) const
{
	if (staleNet(net))
		return std::string(detail::stale_name);
	if (token_sink_ != nullptr)
		return token(Token::Kind::Net, net);
	return sta::Network::pathName(net);
}

bool YosysNetwork::isLinked() const
{
	open_epoch_hook_();
	return linked() && !epoch_closed && view_->upToDate();
}

void YosysNetwork::attach(Yosys::NetView *view)
{
	view_ = view;
	generation_ = (generation_ + 1) & Handle::generation_mask;
	// Named as OpenSTA's verilog reader library
	if (design_lib_ == nullptr)
		design_lib_ = makeLibrary("verilog", "");
}

void YosysNetwork::link()
{
	YInstance *top = view_->top();
	top_cell_ = makeShapedCell(top->hdl_type, false, *top->ports);
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
	// the cells and ports outlive the link
	if (design_lib_ != nullptr)
		retireLibrary(design_lib_);
	design_lib_ = nullptr;
	top_cell_ = nullptr;
	type_cells_.clear();
	inst_cells_.clear();
	pin_ports_.clear();
	port_pins_.clear();
	pin_vertex_ids_.clear();
	child_names_.clear();
	net_names_.clear();
	master_uses_.clear();
	scope_cells_.clear();
	clearNetDrvrPinMap();
}

bool YosysNetwork::ports(const YInstance &inst, std::vector<Netlist::Port> &out)
{
	sta::LibertyCell *lcell = findLibertyCell(inst.type.c_str());
	if (lcell == nullptr && inst.kind == Netlist::Instance::Kind::InternalCell)
		report_->error(2923, "internal cell type {} has no liberty cell, map the design first "
		                     "(techmap/dfflibmap/abc, or icell_liberty).", inst.type);
	if (lcell == nullptr)
		return false;
	sta::CellPortIterator *port_iter = portIterator(cell(lcell));
	while (port_iter->hasNext()) {
		sta::Port *port = port_iter->next();
		Netlist::Port &shape = out.emplace_back();
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
	const sta::LibertyCell *cell = libertyCell(static_cast<sta::Instance *>(handle(&inst)));
	return cell != nullptr && cell->isSequential();
}

bool YosysNetwork::isClockPin(const YPin &pin)
{
	const sta::LibertyPort *port = libertyPort(static_cast<sta::Pin *>(handle(&pin)));
	return port != nullptr && port->isClock();
}

bool YosysNetwork::isBuffer(const YInstance &inst)
{
	const sta::LibertyCell *cell = libertyCell(static_cast<sta::Instance *>(handle(&inst)));
	return cell != nullptr && cell->isBuffer();
}

void YosysNetwork::buildTables(YInstance *scope)
{
	// the cell name stays an alias
	detail::ChildNames &names = child_names_[scope->id];
	for (YInstance *child : scope->children) {
		names[detail::staName(child->unique_name)] = child;
		names.emplace(detail::staName(child->name), child);
	}
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
	std::string key = scope->hdl_type;
	if (master_uses_[scope->hdl_type] > 1)
		key += "\n" + std::to_string(view_->bodyKey(scope));
	auto [it, fresh] = scope_cells_.try_emplace(key, nullptr);
	if (fresh)
		it->second = makeShapedCell(freeCellName(scope), false, *scope->ports);
	return it->second;
}

std::string YosysNetwork::freeCellName(const YInstance *scope) const
{
	if (findCell(design_lib_, scope->hdl_type) == nullptr)
		return scope->hdl_type;
	std::string name = scope->type;
	for (int n = 1; findCell(design_lib_, name) != nullptr; n++)
		name = scope->type + "_" + std::to_string(n);
	return name;
}

void YosysNetwork::countMasters(const YInstance *scope)
{
	for (const YInstance *child : scope->children) {
		if (!child->isLeaf()) {
			master_uses_[child->hdl_type]++;
			countMasters(child);
		}
	}
}

sta::ConcretePort *YosysNetwork::portOf(const YPin *pin) const
{
	sta::ConcreteCell *ccell = inst_cells_.get(pin->inst->id);
	const std::string &raw = pin->port().name;
	std::string port_name = raw;
	if (ccell->libertyCell() == nullptr)
		port_name = detail::staName(raw);
	sta::Port *port = reinterpret_cast<sta::Port *>(ccell->findPort(port_name));
	if (port == nullptr)
		report_->error(2903, "port {} missing on {}.", raw, name(reinterpret_cast<sta::Cell *>(ccell)));
	if (!hasMembers(port))
		return reinterpret_cast<sta::ConcretePort *>(port);
	sta::Port *bit = findBusBit(port, pin->hdlIndex());
	if (bit == nullptr)
		report_->error(2916, "bus bit {} out of range on port {}.", pin->bit, name(port));
	return reinterpret_cast<sta::ConcretePort *>(bit);
}

// The cell and the pin ports of an instance
void YosysNetwork::indexPins(YInstance *inst)
{
	if (inst->isLeaf())
		inst_cells_[inst->id] = typeCell(*inst);
	for (YPin *pin : inst->pins) {
		pin_ports_[pin->id] = portOf(pin);
		port_pins_[{inst->id, pin_ports_[pin->id]}] = pin;
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
		ccell = makeShapedCell(inst.type, true, *inst.ports);
	type_cells_[inst.type] = ccell;
	return ccell;
}

sta::ConcreteCell *YosysNetwork::makeShapedCell(const std::string &cell_name, bool leaf, const std::vector<Netlist::Port> &shapes)
{
	sta::Cell *scell = makeCell(design_lib_, cell_name, leaf, "");
	for (const Netlist::Port &shape : shapes) {
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

void YosysNetwork::retireLibrary(sta::Library *library)
{
	sta::ConcreteLibrary *clib = reinterpret_cast<sta::ConcreteLibrary *>(library);
	library_map_.erase(clib->name());
	library_seq_.erase(std::ranges::find(library_seq_, clib));
	retired_libs_.emplace_back(clib);
}

sta::Instance *YosysNetwork::topInstance() const
{
	if (!linked())
		return nullptr;
	return handle(view_->top());
}

std::string YosysNetwork::name(const sta::Instance *instance) const
{
	const YInstance *inst = recordOf<YInstance>(instance);
	if (inst->isTop())
		return inst->name;
	return detail::staName(inst->unique_name);
}

ObjectId YosysNetwork::id(const sta::Instance *instance) const
{
	return recordOf<YInstance>(instance)->id;
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
	return reinterpret_cast<sta::Cell *>(inst_cells_.get(recordOf<YInstance>(instance)->id));
}

sta::Instance *YosysNetwork::parent(const sta::Instance *instance) const
{
	return handle(recordOf<YInstance>(instance)->parent);
}

bool YosysNetwork::isLeaf(const sta::Instance *instance) const
{
	return recordOf<YInstance>(instance)->isLeaf();
}

sta::Instance *YosysNetwork::findChild(const sta::Instance *parent, std::string_view name) const
{
	return handle(detail::lookup(child_names_.get(recordOf<YInstance>(parent)->id), name));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, std::string_view port_name) const
{
	const YInstance *inst = recordOf<YInstance>(instance);
	const sta::ConcretePort *port = inst_cells_.get(inst->id)->findPort(port_name);
	return handle(detail::lookup(port_pins_, std::pair(inst->id, port)));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, const sta::Port *port) const
{
	return findPin(instance, name(port));
}

// Name order, host-name aliases skipped
sta::InstanceChildIterator *YosysNetwork::childIterator(const sta::Instance *instance) const
{
	std::vector<YInstance *> children;
	for (const auto &[name, child] : child_names_.get(recordOf<YInstance>(instance)->id)) {
		if (name == detail::staName(child->unique_name))
			children.push_back(child);
	}
	return detail::handleIterator<sta::Instance *>(handles(children));
}

sta::InstancePinIterator *YosysNetwork::pinIterator(const sta::Instance *instance) const
{
	std::vector<YPin *> pins = recordOf<YInstance>(instance)->pins;
	detail::reversePortRuns(pins);
	return detail::handleIterator<sta::Pin *>(handles(pins));
}

// Name order, aliases skipped
sta::InstanceNetIterator *YosysNetwork::netIterator(const sta::Instance *instance) const
{
	std::vector<YNet *> nets;
	for (const auto &[name, claim] : net_names_.get(recordOf<YInstance>(instance)->id)) {
		if (claim.canonical)
			nets.push_back(claim.net);
	}
	return detail::handleIterator<sta::Net *>(handles(nets));
}

ObjectId YosysNetwork::id(const sta::Pin *pin) const
{
	return recordOf<YPin>(pin)->id;
}

sta::Instance *YosysNetwork::instance(const sta::Pin *pin) const
{
	return handle(recordOf<YPin>(pin)->inst);
}

sta::Net *YosysNetwork::net(const sta::Pin *pin) const
{
	return handle(recordOf<YPin>(pin)->net);
}

sta::Term *YosysNetwork::term(const sta::Pin *pin) const
{
	const YPin *record = recordOf<YPin>(pin);
	if (record->inner == nullptr)
		return nullptr;
	return handle(record, Handle::Kind::Term);
}

sta::Port *YosysNetwork::port(const sta::Pin *pin) const
{
	return reinterpret_cast<sta::Port *>(pin_ports_.get(recordOf<YPin>(pin)->id));
}

sta::PortDirection *YosysNetwork::direction(const sta::Pin *pin) const
{
	return direction(port(pin));
}

sta::VertexId YosysNetwork::vertexId(const sta::Pin *pin) const
{
	return pin_vertex_ids_.get(recordOf<YPin>(pin)->id);
}

void YosysNetwork::setVertexId(sta::Pin *pin, sta::VertexId id)
{
	pin_vertex_ids_[recordOf<YPin>(pin)->id] = id;
}

ObjectId YosysNetwork::id(const sta::Term *term) const
{
	return recordOf<YPin>(term, Handle::Kind::Term)->id;
}

sta::Net *YosysNetwork::net(const sta::Term *term) const
{
	return handle(recordOf<YPin>(term, Handle::Kind::Term)->inner);
}

sta::Pin *YosysNetwork::pin(const sta::Term *term) const
{
	return handle(recordOf<YPin>(term, Handle::Kind::Term));
}

std::string YosysNetwork::name(const sta::Net *net) const
{
	if (staleNet(net))
		return std::string(detail::stale_name);
	return detail::staNetName(*view_, recordOf<YNet>(net));
}

ObjectId YosysNetwork::id(const sta::Net *net) const
{
	if (net == nullptr)
		return sta::object_id_null;
	return recordOf<YNet>(net)->id;
}

sta::Net *YosysNetwork::findNet(const sta::Instance *instance, std::string_view net_name) const
{
	return handle(detail::lookup(net_names_.get(recordOf<YInstance>(instance)->id), net_name).net);
}

void YosysNetwork::findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern, sta::NetSeq &matches) const
{
	if (!pattern->hasWildcards()) {
		if (sta::Net *net = findNet(instance, pattern->pattern()); net != nullptr)
			matches.push_back(net);
		return;
	}
	std::set<const YNet *> seen;
	for (const auto &[name, claim] : net_names_.get(recordOf<YInstance>(instance)->id)) {
		if (pattern->match(name) && seen.insert(claim.net).second)
			matches.push_back(handle(claim.net));
	}
}

sta::Instance *YosysNetwork::instance(const sta::Net *net) const
{
	// write_sdc sorts it
	if (staleNet(net))
		return topInstance();
	return handle(recordOf<YNet>(net)->scope);
}

// ConcreteNet prepends each connected pin and term
sta::NetPinIterator *YosysNetwork::pinIterator(const sta::Net *net) const
{
	const std::vector<YPin *> &pins = recordOf<YNet>(net)->pins;
	std::vector<YPin *> seq(pins.rbegin(), pins.rend());
	detail::reversePortRuns(seq);
	return detail::handleIterator<const sta::Pin *>(handles(seq));
}

sta::NetTermIterator *YosysNetwork::termIterator(const sta::Net *net) const
{
	const std::vector<YPin *> &ports = recordOf<YNet>(net)->boundary;
	return detail::handleIterator<sta::Term *>(handles(std::vector<YPin *>(ports.rbegin(), ports.rend()), Handle::Kind::Term));
}

bool YosysNetwork::isPower(const sta::Net *net) const
{
	return recordOf<YNet>(net)->constant == Netlist::Net::Const::One;
}

bool YosysNetwork::isGround(const sta::Net *net) const
{
	return recordOf<YNet>(net)->constant == Netlist::Net::Const::Zero;
}

sta::ConstantPinIterator *YosysNetwork::constantPinIterator()
{
	sta::NetSet zero_nets(this);
	sta::NetSet one_nets(this);
	for (YNet *net : view_->constNets(Netlist::Net::Const::Zero))
		zero_nets.insert(handle(net));
	for (YNet *net : view_->constNets(Netlist::Net::Const::One))
		one_nets.insert(handle(net));
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
