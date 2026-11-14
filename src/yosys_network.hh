#ifndef YOSYS_NETWORK_HH
#define YOSYS_NETWORK_HH

// YosysNetwork shows one Yosys::NetView to OpenSTA

#include "kernel/nettrack.h"

#include "sta/ConcreteLibrary.hh"
#include "sta/ConcreteNetwork.hh"
#include "sta/PatternMatch.hh"
#include "sta/Report.hh"

#include <ranges>

namespace ysta {

namespace Netlist = Yosys::Netlist;
using Yosys::NetView;

template <typename T, typename... Types> constexpr bool is_one_of = (std::is_same_v<T, Types> || ...);
// the sta object types a handle stands for
template <typename T>
concept StaObject = is_one_of<std::remove_const_t<T>, sta::Instance, sta::Pin, sta::Net, sta::Term>;

// a view record id packed into an sta object pointer, OpenSTA never gets a record pointer
struct Handle {
	enum class Kind : uint8_t { Instance, Pin, Net, Term };
	static constexpr uint64_t generation_mask = (1u << 29) - 1;

	// 63    handle? (1 => handle)
	// 61-62 kind
	// 32-60 generation
	// 0-31  record id
	uint64_t bits = 0;
	Handle() = default;
	Handle(Kind kind, uint32_t generation, uint32_t id);
	template <StaObject T> Handle(const T *object) : bits(reinterpret_cast<uintptr_t>(object)) {}
	template <StaObject T> operator T *() const { return reinterpret_cast<T *>(bits); }
	Kind kind() const { return Kind(bits >> 61 & 3); }
	uint32_t generation() const { return bits >> 32 & generation_mask; }
	uint32_t id() const { return uint32_t(bits); }
	std::string swigName() const; // the SWIG object name in Tcl
	auto operator<=>(const Handle &) const = default;
};

constexpr Handle::Kind recordKind(const Netlist::Instance *) { return Handle::Kind::Instance; }
constexpr Handle::Kind recordKind(const Netlist::Pin *) { return Handle::Kind::Pin; }
constexpr Handle::Kind recordKind(const Netlist::Net *) { return Handle::Kind::Net; }
template <typename Record> constexpr Handle::Kind record_kind = recordKind(static_cast<const Record *>(nullptr));

namespace detail {

constexpr std::string_view token_prefix = "__ysta_tok_";
// of a net of an earlier network, see ::sta::write_sdc_cmd in trace.tcl
constexpr std::string_view stale_name = "__ysta_stale";

using NameMap = std::map<std::string, Handle, std::less<>>; // handles by sta name

std::string staName(std::string_view raw);
// a flat net takes the hdlname path of its wire for the wire name, see hierName
std::string staNetName(const Netlist::Net *net, std::string path = "");
// u1\/r for the hdlname "u1 r" that flatten gives the cell u1.r, empty for an object of no dissolved scope
std::string hierName(const Yosys::RTLIL::AttrObject *object);
// a name of two objects names none of them
void addName(NameMap &names, const std::string &name, Handle handle);

// pin order as ConcreteNetwork gives it
void reversePortRuns(std::vector<Netlist::Pin *> &pins);
Yosys::RTLIL::PortDir portDir(const sta::PortDirection *direction);
sta::PortDirection *staDirection(Yosys::RTLIL::PortDir dir);

// instance ids count from 0, pin and net ids from 1
template <typename Record> constexpr uint32_t first_id = std::is_same_v<Record, Netlist::Instance> ? 0 : 1;

// the map value, or an empty one
template <typename Map> typename Map::mapped_type lookup(const Map &map, std::string_view key)
{
	auto it = map.find(key);
	if (it == map.end())
		return {};
	return it->second;
}

// adds the handles of the names that match, false when none
template <typename Seq> bool appendMatches(const NameMap &names, const sta::PatternMatch *pattern, Seq &matches)
{
	size_t count = matches.size();
	for (const auto &[name, handle] : names) {
		if (handle.bits != 0 && pattern->match(name))
			matches.push_back(handle);
	}
	return matches.size() != count;
}

// iterator over the sta objects of handles
template <typename T> class SeqIterator : public sta::Iterator<T> {
	std::vector<T> seq_;
	size_t index_ = 0;
public:
	template <typename Range> explicit SeqIterator(const Range &handles) : seq_(handles.begin(), handles.end()) {}
	bool hasNext() override { return index_ < seq_.size(); }
	T next() override { return seq_[index_++]; }
};

} // namespace detail

// errors go through report_->error, which throws sta::Exception
// token mode changes what write_sdc writes, see setTokens
class YosysNetwork : public sta::ConcreteNetwork, public Netlist::CellLibrary {
public:
	using sta::ConcreteNetwork::name;
	using sta::ConcreteNetwork::cell;
	using sta::ConcreteNetwork::direction;

	// queryHook -> Epochs::syncForQuery, relinks if the design changed
	explicit YosysNetwork(bool (*open_epoch_hook)()) : open_epoch_hook_(open_epoch_hook) {}

	// link
	// needs unlinked, returns how many wires have the name of a constant net, linkView warns
	size_t link(std::unique_ptr<NetView> view);
	// drops the view and the tables, epoch.cc calls it before each link and after a throw in sync or link
	void unlink();
	// unlinked, view() refuses, topInstance() is null and the tables are empty
	bool linked() const { return view_ != nullptr; }
	NetView &view() const; // of the link
	// Netlist::CellLibrary from liberty cells, an unmapped internal cell errors
	bool describe(Netlist::CellType &type) override;
	// OpenSTA asks before each network read, see Sta::ensureLinked
	bool isLinked() const override;

	// write_sdc modes
	// token mode while set, see Epochs::capture
	// non-top instances, pins, nets and scalar top ports get token names
	void setTokens(std::map<Handle, size_t> *tokens) { tokens_ = tokens; }
	std::string token(Handle handle) const; // __ysta_tok_N, N is the map value of the handle
	// Tcl links ::ysta::writing_sdc to it, see names_stale_nets_
	int *staleNetNamesFlag() { return &names_stale_nets_; }

	// Instance
	sta::Instance *topInstance() const override;
	std::string name(const sta::Instance *instance) const override;
	sta::ObjectId id(const sta::Instance *instance) const override;
	std::string getAttribute(const sta::Instance *, std::string_view) const override;
	const sta::AttributeMap &attributeMap(const sta::Instance *) const override;
	void setAttribute(sta::Instance *instance, std::string_view key, std::string_view value) override;
	sta::Cell *cell(const sta::Instance *instance) const override;
	sta::Instance *parent(const sta::Instance *instance) const override;
	bool isLeaf(const sta::Instance *instance) const override;
	std::string pathName(const sta::Instance *instance) const override;
	sta::Instance *findChild(const sta::Instance *parent, std::string_view name) const override;
	void findChildrenMatching(const sta::Instance *parent, const sta::PatternMatch *pattern,
	                          sta::InstanceSeq &matches) const override;
	sta::Pin *findPin(const sta::Instance *instance, std::string_view port_name) const override;
	sta::Pin *findPin(const sta::Instance *instance, const sta::Port *port) const override;
	sta::InstanceChildIterator *childIterator(const sta::Instance *instance) const override;
	sta::InstancePinIterator *pinIterator(const sta::Instance *instance) const override;
	sta::InstanceNetIterator *netIterator(const sta::Instance *instance) const override;

	// Pin, and the name of a top Port
	sta::ObjectId id(const sta::Pin *pin) const override;
	sta::Instance *instance(const sta::Pin *pin) const override;
	sta::Net *net(const sta::Pin *pin) const override;
	sta::Term *term(const sta::Pin *pin) const override;
	sta::Port *port(const sta::Pin *pin) const override;
	sta::PortDirection *direction(const sta::Pin *pin) const override;
	sta::VertexId vertexId(const sta::Pin *pin) const override;
	void setVertexId(sta::Pin *pin, sta::VertexId id) override;
	// in token mode, so write_sdc writes a hierarchical -through pin as itself
	bool isLeaf(const sta::Pin *pin) const override;
	std::string pathName(const sta::Pin *pin) const override;
	std::string name(const sta::Port *port) const override;

	// Term
	sta::ObjectId id(const sta::Term *term) const override;
	sta::Net *net(const sta::Term *term) const override;
	sta::Pin *pin(const sta::Term *term) const override;

	// Net
	std::string name(const sta::Net *net) const override;
	sta::ObjectId id(const sta::Net *net) const override;
	std::string pathName(const sta::Net *net) const override;
	// by sta name in the scope, a constant net wins its name
	sta::Net *findNet(const sta::Instance *instance, std::string_view net_name) const override;
	void findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern,
	                          sta::NetSeq &matches) const override;
	sta::Instance *instance(const sta::Net *net) const override;
	sta::NetPinIterator *pinIterator(const sta::Net *net) const override;
	sta::NetTermIterator *termIterator(const sta::Net *net) const override;
	bool isPower(const sta::Net *net) const override;
	bool isGround(const sta::Net *net) const override;

	// handles and records
	// a Term is the inside of a scope pin (pin->inner), its handle is the pin id with Kind::Term
	template <typename Record> Handle handle(const Record *record, Handle::Kind kind = record_kind<Record>) const
	{
		if (record == nullptr)
			return {};
		return {kind, generation_, record->id};
	}
	template <typename Record>
	std::vector<Handle> handles(const std::vector<Record *> &records, Handle::Kind kind = record_kind<Record>) const
	{
		std::vector<Handle> result;
		for (const Record *record : records)
			result.push_back(handle(record, kind));
		return result;
	}
	// decodes a handle of a view table, unlinked or another generation is an error
	template <typename Record>
	const Record *recordOf(Handle handle, const std::deque<Record> &(NetView::*table)() const,
	                       Handle::Kind kind = record_kind<Record>) const
	{
		if (handle.bits == 0)
			return nullptr;
		const std::deque<Record> &records = (view().*table)();
		uint32_t position = handle.id() - detail::first_id<Record>;
		if (position >= records.size() || handle.kind() != kind || handle.generation() != generation_)
			refuseHandle();
		return &records[position];
	}
protected:
	void visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const override;
private:
	// the sta cell of a view cell type
	struct StaCell {
		sta::ConcreteCell *cell = nullptr;
		std::vector<sta::ConcretePort *> bit_ports; // scalar or bus bit, by position in Instance::pins
		Yosys::dict<const sta::ConcretePort *, uint32_t> pin_of_port; // the position of a bit port
	};
	sta::ConcreteCell *concreteCell(const Netlist::Instance *inst);
	sta::ConcreteCell *makeShapedCell(const Netlist::Instance &inst);
	sta::ConcretePort *bitPort(sta::ConcreteCell *ccell, const Netlist::Port &shape, int bit) const;
	size_t buildTables(Netlist::Instance *scope);
	void addCell(const Netlist::Instance *inst);
	void refuseHandle() const; // error 2931, throws, the network stays
	const Netlist::Instance *record(const sta::Instance *inst) const { return recordOf(inst, &NetView::instances); }
	const Netlist::Pin *record(const sta::Pin *pin) const { return recordOf(pin, &NetView::pins); }
	const Netlist::Pin *record(const sta::Term *term) const
	{
		return recordOf(term, &NetView::pins, Handle::Kind::Term);
	}
	const Netlist::Net *record(const sta::Net *net) const { return recordOf(net, &NetView::nets); }
	std::unique_ptr<NetView> view_;
	uint32_t generation_ = 1; // link() bumps it, unlink() keeps it to refuse old handles
	std::map<Handle, size_t> *tokens_ = nullptr; // token mode when set
	int names_stale_nets_ = 0; // write_sdc sets it, a net of an earlier network then has the stale name
	sta::Library *design_lib_ = nullptr;
	bool (*open_epoch_hook_)();
	sta::AttributeMap no_attributes_;
	// libraries of earlier links, OpenSTA can still point into them
	std::vector<std::unique_ptr<sta::ConcreteLibrary>> retired_libs_;
	Yosys::dict<const Netlist::CellType *, StaCell> cells_;
	std::map<std::string, sta::ConcreteCell *> cells_by_key_; // see concreteCell
	std::vector<sta::VertexId> pin_vertex_ids_; // by pin id, in the graph of this link

	// the sta names in a scope, by instance id
	struct ScopeNames {
		detail::NameMap children, nets;
	};
	std::vector<ScopeNames> scope_names_;
	const ScopeNames &scopeNames(const sta::Instance *instance) const;
	// the flat cells and nets of a scope by hdlname path, so u1/r finds the cell u1.r
	// by instance id, built at the first miss of a name with a divider
	mutable std::map<uint32_t, ScopeNames> hier_names_;
	// none for a name with no divider, and when a child has the first name of the path, a real scope wins
	const ScopeNames &hierNames(const sta::Instance *instance, std::string_view name) const;
};

} // namespace ysta

#endif
