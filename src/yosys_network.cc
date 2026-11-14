// SPDX-License-Identifier: GPL-3.0-or-later
#include "yosys_network.hh"

#include "sta/Liberty.hh"
#include "sta/PortDirection.hh"

namespace ysta {

static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "a handle holds a kind, a generation and a record id");

Handle::Handle(Kind kind, uint32_t generation, uint32_t id)
{
	bits = 1ull << 63 | uint64_t(kind) << 61 | (generation & generation_mask) << 32 | id;
}

// little-endian hex then the type, like _40a3..._p_Pin
std::string Handle::swigName() const
{
	static const char *types[] = {"Instance", "Pin", "Net", "Term"};
	return Yosys::stringf("_%016llx_p_%s", (unsigned long long)__builtin_bswap64(bits), types[int(kind())]);
}

// escapes '/', '[', ']' and '\' as OpenSTA's readers store names
// SdcNetwork needs it to resolve esc/inst or a[3]
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

std::string detail::staNetName(const Netlist::Net *net, std::string path)
{
	// VerilogReader names
	if (net->constant == Netlist::Net::Const::One)
		return "one_";
	if (net->constant == Netlist::Net::Const::Zero)
		return "zero_";
	if (path.empty())
		path = detail::staName(net->name.wire);
	return net->name.scalar ? path : path + "[" + std::to_string(net->name.index) + "]";
}

std::string detail::hierName(const Yosys::RTLIL::AttrObject *object)
{
	std::string path;
	if (object != nullptr)
		path = object->get_string_attribute(Yosys::RTLIL::ID::hdlname);
	if (path.find(' ') == std::string::npos)
		return "";
	std::ranges::replace(path, ' ', '/');
	return detail::staName(path);
}

void detail::addName(NameMap &names, const std::string &name, Handle handle)
{
	auto [it, fresh] = names.try_emplace(name, handle);
	if (!fresh)
		it->second = {};
}

void detail::reversePortRuns(std::vector<Netlist::Pin *> &pins)
{
	size_t begin = 0;
	while (begin < pins.size()) {
		size_t end = begin + 1;
		const Netlist::Pin *head = pins[begin];
		while (end < pins.size() && pins[end]->inst == head->inst && pins[end]->port_index == head->port_index)
			end++;
		std::reverse(pins.begin() + begin, pins.begin() + end);
		begin = end;
	}
}

Yosys::RTLIL::PortDir detail::portDir(const sta::PortDirection *direction)
{
	int bits = direction->isAnyInput() * Yosys::RTLIL::PD_INPUT | direction->isAnyOutput() * Yosys::RTLIL::PD_OUTPUT;
	return Yosys::RTLIL::PortDir(bits); // both bits are PD_INOUT
}

sta::PortDirection *detail::staDirection(Yosys::RTLIL::PortDir dir)
{
	// in the order of PortDir
	sta::PortDirection *const directions[] = {sta::PortDirection::unknown(), sta::PortDirection::input(),
	                                          sta::PortDirection::output(), sta::PortDirection::bidirect()};
	return directions[dir];
}

NetView &YosysNetwork::view() const
{
	if (!linked())
		refuseHandle();
	return *view_;
}

void YosysNetwork::refuseHandle() const
{
	report_->error(2931, "a handle of an earlier network names no object, get the object again.");
}

bool YosysNetwork::isLinked() const { return open_epoch_hook_(); }

// one stable token per object, write_sdc sorts by name
std::string YosysNetwork::token(Handle handle) const
{
	size_t index = tokens_->try_emplace(handle, tokens_->size()).first->second;
	return std::string(detail::token_prefix) + std::to_string(index);
}

std::string YosysNetwork::pathName(const sta::Instance *instance) const
{
	if (tokens_ != nullptr && instance != topInstance())
		return token(instance);
	return sta::Network::pathName(instance);
}

// write_sdc writes top ports by name
std::string YosysNetwork::name(const sta::Port *port) const
{
	sta::Pin *top_pin = nullptr;
	if (tokens_ != nullptr && cell(port) == cell(topInstance()))
		top_pin = findPin(topInstance(), sta::ConcreteNetwork::name(port));
	// a bus port has no pin, it stays a name
	if (top_pin != nullptr)
		return token(top_pin);
	return sta::ConcreteNetwork::name(port);
}

std::string YosysNetwork::pathName(const sta::Pin *pin) const
{
	if (tokens_ != nullptr)
		return token(pin);
	return sta::Network::pathName(pin);
}

bool YosysNetwork::isLeaf(const sta::Pin *pin) const { return tokens_ != nullptr || sta::Network::isLeaf(pin); }

std::string YosysNetwork::pathName(const sta::Net *net) const
{
	if (tokens_ != nullptr && Handle(net).generation() == generation_)
		return token(net);
	return sta::Network::pathName(net);
}

size_t YosysNetwork::link(std::unique_ptr<NetView> view)
{
	log_assert(!linked());
	generation_ = (generation_ + 1) & Handle::generation_mask;
	view_ = std::move(view);
	// named as OpenSTA's verilog reader library
	design_lib_ = makeLibrary("verilog", "");
	Netlist::Instance *top = view_->top();
	addCell(top);
	pin_vertex_ids_.assign(view_->pins().size() + 1, 0);
	scope_names_.resize(view_->instances().size());
	size_t shared_names = buildTables(top);
	if (view_->pins().size() >= 0x40000000)
		report_->error(2927, "link: too many pins for 30-bit object ids.");
	return shared_names;
}

void YosysNetwork::unlink()
{
	// the cells and ports outlive the link
	if (design_lib_ != nullptr) {
		sta::ConcreteLibrary *clib = reinterpret_cast<sta::ConcreteLibrary *>(design_lib_);
		library_map_.erase(clib->name());
		library_seq_.erase(std::ranges::find(library_seq_, clib));
		retired_libs_.emplace_back(clib);
	}
	design_lib_ = nullptr;
	cells_by_key_.clear();
	cells_.clear();
	pin_vertex_ids_.clear();
	scope_names_.clear();
	hier_names_.clear();
	clearConstantNets();
	view_.reset();
	clearNetDrvrPinMap();
}

bool YosysNetwork::describe(Netlist::CellType &type)
{
	sta::LibertyCell *lcell = findLibertyCell(type.name.c_str());
	if (lcell == nullptr && type.kind == Netlist::CellType::Kind::InternalCell)
		report_->error(2923, "internal cell type {} has no liberty cell, map the design first "
		                     "(techmap/dfflibmap/abc, or icell_liberty).", type.name);
	if (lcell == nullptr)
		return false;
	type.is_buffer = lcell->isBuffer();
	std::unique_ptr<sta::CellPortIterator> port_iter(portIterator(cell(lcell)));
	while (port_iter->hasNext()) {
		sta::Port *port = port_iter->next();
		Netlist::Port &shape = type.ports.emplace_back();
		shape.name = name(port);
		shape.width = size(port);
		shape.vector = hasMembers(port);
		if (shape.vector) {
			shape.from = fromIndex(port);
			shape.to = toIndex(port);
		}
		shape.dir = detail::portDir(direction(port));
	}
	return true;
}

size_t YosysNetwork::buildTables(Netlist::Instance *scope)
{
	ScopeNames &names = scope_names_[scope->id];
	size_t shared_names = 0;
	for (Netlist::Instance *child : scope->children) {
		names.children.emplace(detail::staName(child->name), handle(child));
		if (!child->isLeaf())
			shared_names += buildTables(child);
		addCell(child);
	}
	// a wire bit has one name, a constant net takes its name from a wire
	for (Netlist::Net *net : scope->nets) {
		Handle &owner = names.nets[detail::staNetName(net)];
		shared_names += owner.bits != 0;
		if (owner.bits == 0 || net->constant != Netlist::Net::Const::None)
			owner = handle(net);
		bool one = net->constant == Netlist::Net::Const::One;
		if (net->constant != Netlist::Net::Const::None)
			addConstantNet(handle(net), one ? sta::LogicValue::one : sta::LogicValue::zero);
	}
	return shared_names;
}

void YosysNetwork::addCell(const Netlist::Instance *inst)
{
	if (cells_.count(inst->type))
		return;
	sta::ConcreteCell *ccell = concreteCell(inst);
	StaCell &entry = cells_[inst->type];
	entry.cell = ccell;
	for (const Netlist::Port &shape : inst->type->ports) {
		for (int bit = 0; bit < shape.width; bit++) {
			sta::ConcretePort *port = bitPort(ccell, shape, bit);
			entry.pin_of_port[port] = entry.bit_ports.size();
			entry.bit_ports.push_back(port);
		}
	}
}

// a leaf takes its liberty cell, else the cell of its type name
// scopes with one source name and equal bodies share a cell
sta::ConcreteCell *YosysNetwork::concreteCell(const Netlist::Instance *inst)
{
	const Netlist::CellType &type = *inst->type;
	std::string key = type.hdl_name;
	if (inst->isLeaf()) {
		if (sta::LibertyCell *lcell = findLibertyCell(type.name.c_str()); lcell != nullptr)
			return reinterpret_cast<sta::ConcreteCell *>(cell(lcell));
	} else {
		key += "\n" + std::to_string(type.body_key);
	}
	auto [it, fresh] = cells_by_key_.try_emplace(key, nullptr);
	if (fresh)
		it->second = makeShapedCell(*inst);
	return it->second;
}

sta::ConcretePort *YosysNetwork::bitPort(sta::ConcreteCell *ccell, const Netlist::Port &shape, int bit) const
{
	std::string port_name = shape.name;
	if (ccell->libertyCell() == nullptr)
		port_name = detail::staName(shape.name);
	sta::Port *port = reinterpret_cast<sta::Port *>(ccell->findPort(port_name));
	if (port == nullptr)
		report_->error(2903, "port {} missing on {}.", shape.name, name(reinterpret_cast<sta::Cell *>(ccell)));
	if (!hasMembers(port))
		return reinterpret_cast<sta::ConcretePort *>(port);
	sta::Port *member = findBusBit(port, shape.hdlIndex(bit));
	if (member == nullptr)
		report_->error(2916, "bus bit {} out of range on port {}.", bit, name(port));
	return reinterpret_cast<sta::ConcretePort *>(member);
}

sta::ConcreteCell *YosysNetwork::makeShapedCell(const Netlist::Instance &inst)
{
	std::string cell_name = inst.type->hdl_name;
	for (int n = 0; findCell(design_lib_, cell_name) != nullptr; n++) {
		cell_name = inst.type->name;
		if (n > 0)
			cell_name += "_" + std::to_string(n);
	}
	sta::Cell *scell = makeCell(design_lib_, cell_name, inst.isLeaf(), "");
	for (const Netlist::Port &shape : inst.type->ports) {
		std::string port_name = detail::staName(shape.name);
		bool scalar = shape.width == 1 && !shape.vector;
		sta::Port *port = scalar ? makePort(scell, port_name) : makeBusPort(scell, port_name, shape.from, shape.to);
		setDirection(port, detail::staDirection(shape.dir));
	}
	return reinterpret_cast<sta::ConcreteCell *>(scell);
}

const YosysNetwork::ScopeNames &YosysNetwork::scopeNames(const sta::Instance *instance) const
{
	return scope_names_[record(instance)->id];
}

const YosysNetwork::ScopeNames &YosysNetwork::hierNames(const sta::Instance *instance, std::string_view name) const
{
	static const ScopeNames none;
	size_t divider = name.find("\\/");
	if (divider == std::string_view::npos || scopeNames(instance).children.contains(name.substr(0, divider)))
		return none;
	const Netlist::Instance *scope = record(instance);
	auto [it, fresh] = hier_names_.try_emplace(scope->id);
	ScopeNames &names = it->second;
	if (!fresh)
		return names;
	for (const Netlist::Instance *child : scope->children) {
		if (std::string path = detail::hierName(child->cell); !path.empty())
			detail::addName(names.children, path, handle(child));
	}
	for (const Netlist::Net *net : scope->nets) {
		// a constant net has no wire
		if (std::string path = detail::hierName(net->name_bit.wire); !path.empty())
			detail::addName(names.nets, detail::staNetName(net, path), handle(net));
	}
	return names;
}

sta::Instance *YosysNetwork::topInstance() const { return linked() ? handle(view_->top()) : Handle(); }

std::string YosysNetwork::name(const sta::Instance *instance) const
{
	const Netlist::Instance *inst = record(instance);
	return inst->isTop() ? inst->name : detail::staName(inst->name);
}

sta::ObjectId YosysNetwork::id(const sta::Instance *instance) const { return record(instance)->id; }

std::string YosysNetwork::getAttribute(const sta::Instance *, std::string_view) const { return ""; }

const sta::AttributeMap &YosysNetwork::attributeMap(const sta::Instance *) const { return no_attributes_; }

sta::Cell *YosysNetwork::cell(const sta::Instance *instance) const
{
	return reinterpret_cast<sta::Cell *>(cells_.at(record(instance)->type).cell);
}

sta::Instance *YosysNetwork::parent(const sta::Instance *instance) const { return handle(record(instance)->parent); }

bool YosysNetwork::isLeaf(const sta::Instance *instance) const { return record(instance)->isLeaf(); }

sta::Instance *YosysNetwork::findChild(const sta::Instance *parent, std::string_view name) const
{
	Handle child = detail::lookup(scopeNames(parent).children, name);
	if (child.bits == 0)
		child = detail::lookup(hierNames(parent, name).children, name);
	return child;
}

void YosysNetwork::findChildrenMatching(const sta::Instance *parent, const sta::PatternMatch *pattern,
                                        sta::InstanceSeq &matches) const
{
	if (!pattern->hasWildcards())
		sta::Network::findChildrenMatching(parent, pattern, matches);
	else if (!detail::appendMatches(scopeNames(parent).children, pattern, matches))
		detail::appendMatches(hierNames(parent, pattern->pattern()).children, pattern, matches);
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, std::string_view port_name) const
{
	const Netlist::Instance *inst = record(instance);
	const StaCell &entry = cells_.at(inst->type);
	auto it = entry.pin_of_port.find(entry.cell->findPort(port_name));
	if (it == entry.pin_of_port.end())
		return nullptr;
	return handle(inst->pins[it->second]);
}

sta::Pin *YosysNetwork::findPin(const sta::Instance *instance, const sta::Port *port) const
{
	return findPin(instance, name(port));
}

// name order
sta::InstanceChildIterator *YosysNetwork::childIterator(const sta::Instance *instance) const
{
	return new detail::SeqIterator<sta::Instance *>(std::views::values(scopeNames(instance).children));
}

sta::InstancePinIterator *YosysNetwork::pinIterator(const sta::Instance *instance) const
{
	std::vector<Netlist::Pin *> pins = record(instance)->pins;
	detail::reversePortRuns(pins);
	return new detail::SeqIterator<sta::Pin *>(handles(pins));
}

// name order
sta::InstanceNetIterator *YosysNetwork::netIterator(const sta::Instance *instance) const
{
	return new detail::SeqIterator<sta::Net *>(std::views::values(scopeNames(instance).nets));
}

sta::ObjectId YosysNetwork::id(const sta::Pin *pin) const { return record(pin)->id; }

sta::Instance *YosysNetwork::instance(const sta::Pin *pin) const { return handle(record(pin)->inst); }

sta::Net *YosysNetwork::net(const sta::Pin *pin) const { return handle(record(pin)->net); }

sta::Term *YosysNetwork::term(const sta::Pin *pin) const
{
	const Netlist::Pin *scope_pin = record(pin);
	if (scope_pin->inner == nullptr)
		return nullptr;
	return handle(scope_pin, Handle::Kind::Term);
}

sta::Port *YosysNetwork::port(const sta::Pin *pin) const
{
	const Netlist::Pin *bit_pin = record(pin);
	const StaCell &entry = cells_.at(bit_pin->inst->type);
	return reinterpret_cast<sta::Port *>(entry.bit_ports[bit_pin->port().first_pin + bit_pin->bit]);
}

sta::PortDirection *YosysNetwork::direction(const sta::Pin *pin) const { return direction(port(pin)); }

sta::VertexId YosysNetwork::vertexId(const sta::Pin *pin) const { return pin_vertex_ids_[record(pin)->id]; }

void YosysNetwork::setVertexId(sta::Pin *pin, sta::VertexId id) { pin_vertex_ids_[record(pin)->id] = id; }

sta::ObjectId YosysNetwork::id(const sta::Term *term) const { return record(term)->id; }

sta::Net *YosysNetwork::net(const sta::Term *term) const { return handle(record(term)->inner); }

sta::Pin *YosysNetwork::pin(const sta::Term *term) const { return handle(record(term)); }

std::string YosysNetwork::name(const sta::Net *net) const
{
	if (names_stale_nets_ && Handle(net).generation() != generation_)
		return std::string(detail::stale_name);
	return detail::staNetName(record(net));
}

sta::ObjectId YosysNetwork::id(const sta::Net *net) const
{
	return net != nullptr ? record(net)->id : sta::object_id_null;
}

sta::Net *YosysNetwork::findNet(const sta::Instance *instance, std::string_view net_name) const
{
	Handle net = detail::lookup(scopeNames(instance).nets, net_name);
	if (net.bits == 0)
		net = detail::lookup(hierNames(instance, net_name).nets, net_name);
	return net;
}

void YosysNetwork::findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern,
                                        sta::NetSeq &matches) const
{
	if (!pattern->hasWildcards()) {
		if (sta::Net *net = findNet(instance, pattern->pattern()); net != nullptr)
			matches.push_back(net);
		return;
	}
	if (!detail::appendMatches(scopeNames(instance).nets, pattern, matches))
		detail::appendMatches(hierNames(instance, pattern->pattern()).nets, pattern, matches);
}

sta::Instance *YosysNetwork::instance(const sta::Net *net) const
{
	if (names_stale_nets_ && Handle(net).generation() != generation_)
		return topInstance(); // write_sdc sorts the stale name
	return handle(record(net)->scope);
}

// ConcreteNet prepends each connected pin and term
sta::NetPinIterator *YosysNetwork::pinIterator(const sta::Net *net) const
{
	const std::vector<Netlist::Pin *> &pins = record(net)->pins;
	std::vector<Netlist::Pin *> seq(pins.rbegin(), pins.rend());
	detail::reversePortRuns(seq);
	return new detail::SeqIterator<const sta::Pin *>(handles(seq));
}

sta::NetTermIterator *YosysNetwork::termIterator(const sta::Net *net) const
{
	const std::vector<Netlist::Pin *> &ports = record(net)->boundary;
	std::vector<Netlist::Pin *> reversed_ports(ports.rbegin(), ports.rend());
	return new detail::SeqIterator<sta::Term *>(handles(reversed_ports, Handle::Kind::Term));
}

bool YosysNetwork::isPower(const sta::Net *net) const { return record(net)->constant == Netlist::Net::Const::One; }

bool YosysNetwork::isGround(const sta::Net *net) const { return record(net)->constant == Netlist::Net::Const::Zero; }

void YosysNetwork::visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const
{
	sta::Network::visitConnectedPins(net, visitor, visited_nets);
}

void YosysNetwork::setAttribute(sta::Instance *, std::string_view, std::string_view)
{
	report_->error(2929, "set_attribute: network edits are not supported, edit the design in yosys.");
}

} // namespace ysta
