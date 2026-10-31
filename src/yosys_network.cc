// SPDX-License-Identifier: GPL-3.0-or-later
#include "yosys_network.hh"

#include <algorithm>

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

template <typename T, typename Record> sta::Iterator<T> *handleIterator(const YosysNetwork &network, const std::vector<Record *> &records)
{
	std::vector<T> seq;
	for (Record *record : records)
		seq.push_back(network.handle(record));
	return new detail::SeqIterator<T>(std::move(seq));
}

} // namespace detail

static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "a handle holds a kind, a generation and a record id");

uintptr_t detail::handleValue(HandleKind kind, uint32_t generation, uint32_t id)
{
	uintptr_t tag_and_kind = (static_cast<uintptr_t>(1) << 31) | (static_cast<uintptr_t>(kind) << 29);
	uintptr_t high_word = tag_and_kind | (generation & detail::handle_generation_mask);
	return (high_word << 32) | id;
}

uint32_t detail::handleGeneration(const void *handle)
{
	return (reinterpret_cast<uintptr_t>(handle) >> 32) & detail::handle_generation_mask;
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

size_t detail::scopeNetNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, NetNames &names)
{
	Yosys::NetView::ScopeNames scope_names = netlist.netNames(scope);
	for (const Netlist::OwnedName &owned : scope_names.names)
		names[detail::staWireName(owned.name)] = {owned.net, owned.canonical};
	size_t shared = scope_names.shared;
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

std::string detail::staChildName(const Netlist::Instance *child, const std::string &name)
{
	if (name == child->localName())
		return detail::staInstName(child);
	return detail::staName(child->name);
}

std::vector<std::string> detail::scopeChildNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, ChildNames &children)
{
	std::vector<std::string> names = netlist.childNames(scope);
	for (size_t i = 0; i < names.size(); i++) {
		Netlist::Instance *child = scope->children[i];
		names[i] = detail::staChildName(child, names[i]);
		children[names[i]] = child;
		std::string own = detail::staName(child->name);
		if (own != names[i])
			children.emplace(own, child);
	}
	return names;
}

void YosysNetwork::refuseHandle() const
{
	report_->error(2931, "a handle of an earlier network names no object, get the object again.");
}

uint32_t YosysNetwork::handleId(const void *sta_handle, detail::HandleKind kind) const
{
	uintptr_t value = reinterpret_cast<uintptr_t>(sta_handle);
	uintptr_t expected = detail::handleValue(kind, generation_, 0);
	if (value >> 32 != expected >> 32)
		refuseHandle();
	return static_cast<uint32_t>(value);
}

sta::Instance *YosysNetwork::handle(const YInstance *inst) const
{
	if (inst == nullptr)
		return nullptr;
	return reinterpret_cast<sta::Instance *>(detail::handleValue(detail::HandleKind::Instance, generation_, inst->id));
}

sta::Pin *YosysNetwork::handle(const YPin *pin) const
{
	if (pin == nullptr)
		return nullptr;
	return reinterpret_cast<sta::Pin *>(detail::handleValue(detail::HandleKind::Pin, generation_, pin->id));
}

sta::Net *YosysNetwork::handle(const YNet *net) const
{
	if (net == nullptr)
		return nullptr;
	return reinterpret_cast<sta::Net *>(detail::handleValue(detail::HandleKind::Net, generation_, net->id));
}

sta::Term *YosysNetwork::handle(const YTerm *term) const
{
	if (term == nullptr)
		return nullptr;
	return reinterpret_cast<sta::Term *>(detail::handleValue(detail::HandleKind::Term, generation_, term->id));
}

// error 2931 for a handle of an earlier link or of a cleared view
template <typename Record>
const Record *YosysNetwork::recordOf(const void *sta_handle, detail::HandleKind kind, const Netlist::IdMap<Record *> &records) const
{
	if (sta_handle == nullptr)
		return nullptr;
	const Record *record = records.get(handleId(sta_handle, kind));
	if (record == nullptr)
		refuseHandle();
	return record;
}

const YInstance *YosysNetwork::asInst(const sta::Instance *inst) const
{
	return recordOf(inst, detail::HandleKind::Instance, inst_records_);
}

const YPin *YosysNetwork::asPin(const sta::Pin *pin) const
{
	return recordOf(pin, detail::HandleKind::Pin, pin_records_);
}

const YNet *YosysNetwork::asNet(const sta::Net *net) const
{
	return recordOf(net, detail::HandleKind::Net, net_records_);
}

const YTerm *YosysNetwork::asTerm(const sta::Term *term) const
{
	return recordOf(term, detail::HandleKind::Term, term_records_);
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

void YosysNetwork::setStaleNames(bool on)
{
	stale_names_ = on;
}

bool YosysNetwork::staleNet(const sta::Net *net) const
{
	return stale_names_ && net != nullptr && detail::handleGeneration(net) != generation_;
}

// one stable token per object, write_sdc sorts by name
std::string YosysNetwork::token(Token::Kind kind, const void *object) const
{
	auto [it, fresh] = token_index_.try_emplace(object, token_sink_->size());
	if (fresh)
		token_sink_->push_back({kind, object});
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
	if (token_sink_ != nullptr && cell(port) == reinterpret_cast<sta::Cell *>(top_cell_))
		return token(Token::Kind::TopPort, port);
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
	return linked() && epoch_phase_ == EpochPhase::Open && view_->upToDate();
}

void YosysNetwork::attach(Yosys::NetView *view)
{
	view_ = view;
	generation_ = (generation_ + 1) & detail::handle_generation_mask;
	// Named as OpenSTA's verilog reader library
	if (design_lib_ == nullptr)
		design_lib_ = makeLibrary("verilog", "");
}

void YosysNetwork::link()
{
	YInstance *top = view_->top();
	top_cell_ = makeShapedCell(top->hdl_type, StaCellKind::Hierarchical, *top->ports);
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
	// the cells and ports outlive the link
	if (design_lib_ != nullptr)
		retireLibrary(design_lib_);
	design_lib_ = nullptr;
	top_cell_ = nullptr;
	type_cells_.clear();
	inst_cells_.clear();
	pin_ports_.clear();
	pin_vertex_ids_.clear();
	inst_records_.clear();
	pin_records_.clear();
	net_records_.clear();
	term_records_.clear();
	child_names_.clear();
	inst_names_.clear();
	net_names_.clear();
	pin_names_.clear();
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
	const sta::LibertyCell *cell = libertyCell(handle(&inst));
	return cell != nullptr && cell->isSequential();
}

bool YosysNetwork::isClockPin(const YPin &pin)
{
	const sta::LibertyPort *port = libertyPort(handle(&pin));
	return port != nullptr && port->isClock();
}

bool YosysNetwork::isBuffer(const YInstance &inst)
{
	const sta::LibertyCell *cell = libertyCell(handle(&inst));
	return cell != nullptr && cell->isBuffer();
}

void YosysNetwork::buildTables(YInstance *scope)
{
	std::vector<std::string> names = detail::scopeChildNames(*view_, scope, child_names_[scope->id]);
	for (size_t i = 0; i < names.size(); i++)
		inst_names_[scope->children[i]->id] = names[i];
	for (YInstance *child : scope->children) {
		if (!child->isLeaf()) {
			buildTables(child);
			inst_cells_[child->id] = scopeCell(child);
		}
		indexPins(child);
	}
	for (YNet *net : view_->nets(scope))
		net_records_[net->id] = net;
	shared_name_count_ += detail::scopeNetNames(*view_, scope, net_names_[scope->id]);
}

sta::ConcreteCell *YosysNetwork::scopeCell(const YInstance *scope)
{
	std::string key = scope->hdl_type;
	if (master_uses_[scope->hdl_type] > 1)
		key += "\n" + view_->bodyKey(scope);
	auto [it, fresh] = scope_cells_.try_emplace(key, nullptr);
	if (fresh)
		it->second = makeShapedCell(freeCellName(scope), StaCellKind::Hierarchical, *scope->ports);
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

// Record, cell, pin ports and port-bit names of an instance
void YosysNetwork::indexPins(YInstance *inst)
{
	if (inst->isLeaf())
		inst_cells_[inst->id] = typeCell(*inst);
	inst_records_[inst->id] = inst;
	detail::PinNames &names = pin_names_[inst->id];
	for (YPin *pin : inst->pins) {
		pin_records_[pin->id] = pin;
		if (pin->term != nullptr)
			term_records_[pin->term->id] = pin->term;
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

sta::ConcreteCell *YosysNetwork::makeShapedCell(const std::string &cell_name, StaCellKind kind, const std::vector<Netlist::Port> &shapes)
{
	sta::Cell *scell = makeCell(design_lib_, cell_name, kind == StaCellKind::Leaf, "");
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
	return handle(asInst(instance)->parent);
}

bool YosysNetwork::isLeaf(const sta::Instance *instance) const
{
	return asInst(instance)->isLeaf();
}

sta::Instance *YosysNetwork::findChild(const sta::Instance *parent, std::string_view name) const
{
	return handle(detail::lookup(child_names_.get(asInst(parent)->id), name));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, std::string_view port_name) const
{
	return handle(detail::lookup(pin_names_.get(asInst(instance)->id), port_name));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, const sta::Port *port) const
{
	return handle(detail::lookup(pin_names_.get(asInst(instance)->id), name(port)));
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, const sta::LibertyPort *port) const
{
	return handle(detail::lookup(pin_names_.get(asInst(instance)->id), port->name()));
}

// Name order, host-name aliases skipped
sta::InstanceChildIterator *YosysNetwork::childIterator(const sta::Instance *instance) const
{
	std::vector<YInstance *> children;
	for (const auto &[name, child] : child_names_.get(asInst(instance)->id)) {
		if (name == instName(child))
			children.push_back(child);
	}
	return detail::handleIterator<sta::Instance *>(*this, children);
}

sta::InstancePinIterator *YosysNetwork::pinIterator(const sta::Instance *instance) const
{
	std::vector<YPin *> pins = asInst(instance)->pins;
	detail::reversePortRuns(pins);
	return detail::handleIterator<sta::Pin *>(*this, pins);
}

// Name order, aliases skipped
sta::InstanceNetIterator *YosysNetwork::netIterator(const sta::Instance *instance) const
{
	std::vector<YNet *> nets;
	for (const auto &[name, claim] : net_names_.get(asInst(instance)->id)) {
		if (claim.canonical)
			nets.push_back(claim.net);
	}
	return detail::handleIterator<sta::Net *>(*this, nets);
}

ObjectId YosysNetwork::id(const sta::Pin *pin) const
{
	return asPin(pin)->id;
}

sta::Instance *YosysNetwork::instance(const sta::Pin *pin) const
{
	return handle(asPin(pin)->inst);
}

sta::Net *YosysNetwork::net(const sta::Pin *pin) const
{
	return handle(asPin(pin)->net);
}

sta::Term *YosysNetwork::term(const sta::Pin *pin) const
{
	return handle(asPin(pin)->term);
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
	return pin_vertex_ids_.get(asPin(pin)->id);
}

void YosysNetwork::setVertexId(sta::Pin *pin, sta::VertexId id)
{
	pin_vertex_ids_[asPin(pin)->id] = id;
}

ObjectId YosysNetwork::id(const sta::Term *term) const
{
	return asTerm(term)->id;
}

sta::Net *YosysNetwork::net(const sta::Term *term) const
{
	return handle(asTerm(term)->net);
}

sta::Pin *YosysNetwork::pin(const sta::Term *term) const
{
	return handle(asTerm(term)->pin);
}

std::string YosysNetwork::name(const sta::Net *net) const
{
	if (staleNet(net))
		return std::string(detail::stale_name);
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
	return handle(detail::lookup(net_names_.get(asInst(instance)->id), net_name).net);
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
			matches.push_back(handle(claim.net));
	}
}

sta::Instance *YosysNetwork::instance(const sta::Net *net) const
{
	// write_sdc sorts it
	if (staleNet(net))
		return topInstance();
	return handle(asNet(net)->scope);
}

// ConcreteNet prepends each connected pin and term
sta::NetPinIterator *YosysNetwork::pinIterator(const sta::Net *net) const
{
	const std::vector<YPin *> &pins = asNet(net)->pins;
	std::vector<YPin *> seq(pins.rbegin(), pins.rend());
	detail::reversePortRuns(seq);
	return detail::handleIterator<const sta::Pin *>(*this, seq);
}

sta::NetTermIterator *YosysNetwork::termIterator(const sta::Net *net) const
{
	const std::vector<YTerm *> &terms = asNet(net)->terms;
	return detail::handleIterator<sta::Term *>(*this, std::vector<YTerm *>(terms.rbegin(), terms.rend()));
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
