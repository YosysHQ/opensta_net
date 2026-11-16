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
	unsigned long long swapped_bits = __builtin_bswap64(bits);
	return Yosys::stringf("_%016llx_p_%s", swapped_bits, types[int(kind())]);
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

// a flat net takes the hdlname path of its wire for the wire name, see hierName
std::string detail::staNetName(const Netlist::Net *net, std::string path)
{
	// VerilogReader names
	if (net->constant == Netlist::Net::Const::One)
		return "one_";
	if (net->constant == Netlist::Net::Const::Zero)
		return "zero_";
	if (path.empty())
		path = detail::staName(net->name.wire);
	if (net->name.scalar)
		return path;
	return path + "[" + std::to_string(net->name.index) + "]";
}

// u1\/r for the hdlname "u1 r" that flatten gives the cell u1.r, empty for an object of no dissolved scope
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

// a name of two objects names none of them
void detail::addName(NameMap &names, const std::string &name, Handle handle)
{
	auto [it, fresh] = names.try_emplace(name, handle);
	if (!fresh)
		it->second = {};
}

// pin order as ConcreteNetwork gives it
void detail::reversePortRuns(Netlist::PinList &pins)
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
	// PortDir order
	sta::PortDirection *const directions[] = {
		sta::PortDirection::unknown(),
		sta::PortDirection::input(),
		sta::PortDirection::output(),
		sta::PortDirection::bidirect(),
	};
	return directions[dir];
}

NetView &YosysNetwork::view() const
{
	if (!linked())
		refuseHandle();
	return *view_;
}

// throws, the network stays
void YosysNetwork::refuseHandle() const
{
	report_->error(2931, "a handle of an earlier network names no object, get the object again.");
}

// OpenSTA asks before each network read, see Sta::ensureLinked
bool YosysNetwork::isLinked() const { return open_epoch_hook_(); }

// one stable token per object, write_sdc sorts by name
std::string YosysNetwork::token(Handle handle) const
{
	auto [entry, fresh] = tokens_->try_emplace(handle, tokens_->size());
	return std::string(detail::token_prefix) + std::to_string(entry->second);
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

// in token mode, so write_sdc writes a hierarchical -through pin as itself
bool YosysNetwork::isLeaf(const sta::Pin *pin) const { return tokens_ != nullptr || sta::Network::isLeaf(pin); }

std::string YosysNetwork::pathName(const sta::Net *net) const
{
	if (tokens_ != nullptr && Handle(net).generation() == generation_)
		return token(net);
	return sta::Network::pathName(net);
}

// count of wires that have the name of a constant net, linkView warns
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

// epoch.cc calls it before each link and after a throw in sync or link
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
		bool is_constant = net->constant != Netlist::Net::Const::None;
		if (owner.bits == 0 || is_constant)
			owner = handle(net);
		if (!is_constant)
			continue;
		bool is_one = net->constant == Netlist::Net::Const::One;
		sta::LogicValue value = is_one ? sta::LogicValue::one : sta::LogicValue::zero;
		addConstantNet(handle(net), value);
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
		sta::Port *port = nullptr;
		if (shape.width == 1 && !shape.vector)
			port = makePort(scell, port_name);
		else
			port = makeBusPort(scell, port_name, shape.from, shape.to);
		setDirection(port, detail::staDirection(shape.dir));
	}
	return reinterpret_cast<sta::ConcreteCell *>(scell);
}

const YosysNetwork::ScopeNames &YosysNetwork::scopeNames(const sta::Instance *instance) const
{
	return scope_names_[record(instance)->id];
}

// flat cells and nets by hdlname path, u1/r finds the cell u1.r, built at the first miss of a name with a divider
// none when a child has the first name of the path, a real scope wins
const YosysNetwork::ScopeNames &YosysNetwork::hierNames(const sta::Instance *instance, std::string_view name) const
{
	static const ScopeNames none;
	size_t divider = name.find("\\/");
	if (divider == std::string_view::npos)
		return none;
	std::string_view first_name = name.substr(0, divider);
	if (scopeNames(instance).children.contains(first_name))
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

sta::Instance *YosysNetwork::topInstance() const
{
	if (!linked())
		return nullptr;
	return handle(view_->top());
}

std::string YosysNetwork::name(const sta::Instance *instance) const
{
	const Netlist::Instance *inst = record(instance);
	if (inst->isTop())
		return inst->name;
	return detail::staName(inst->name);
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
	Handle child = detail::valueOrEmpty(scopeNames(parent).children, name);
	if (child.bits == 0)
		child = detail::valueOrEmpty(hierNames(parent, name).children, name);
	return child;
}

void YosysNetwork::findChildrenMatching(const sta::Instance *parent, const sta::PatternMatch *pattern, sta::InstanceSeq &matches) const
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
	const std::vector<Netlist::Pin *> &inst_pins = record(instance)->pins;
	Netlist::PinList pins(inst_pins.begin(), inst_pins.end());
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
	size_t position = bit_pin->port().first_pin + bit_pin->bit;
	return reinterpret_cast<sta::Port *>(entry.bit_ports[position]);
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

// a constant net wins its name
sta::Net *YosysNetwork::findNet(const sta::Instance *instance, std::string_view net_name) const
{
	Handle net = detail::valueOrEmpty(scopeNames(instance).nets, net_name);
	if (net.bits == 0)
		net = detail::valueOrEmpty(hierNames(instance, net_name).nets, net_name);
	return net;
}

void YosysNetwork::findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern, sta::NetSeq &matches) const
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
	Netlist::PinList seq(pins.rbegin(), pins.rend());
	detail::reversePortRuns(seq);
	return new detail::SeqIterator<const sta::Pin *>(handles(seq));
}

sta::NetTermIterator *YosysNetwork::termIterator(const sta::Net *net) const
{
	const std::vector<Netlist::Pin *> &ports = record(net)->boundary;
	Netlist::PinList reversed_ports(ports.rbegin(), ports.rend());
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
