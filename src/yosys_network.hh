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
template <typename T>
concept StaObject = is_one_of<std::remove_const_t<T>, sta::Instance, sta::Pin, sta::Net, sta::Term>;

// view record id packed into an sta object pointer, OpenSTA gets no record pointer
struct Handle {
	enum class Kind : uint8_t { Instance, Pin, Net, Term };
	static constexpr uint64_t generation_mask = (1u << 29) - 1;

	// 63    1 for a handle
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
	std::string swigName() const;
	auto operator<=>(const Handle &) const = default;
};

constexpr Handle::Kind recordKind(const Netlist::Instance *) { return Handle::Kind::Instance; }
constexpr Handle::Kind recordKind(const Netlist::Pin *) { return Handle::Kind::Pin; }
constexpr Handle::Kind recordKind(const Netlist::Net *) { return Handle::Kind::Net; }
template <typename Record> constexpr Handle::Kind record_kind = recordKind(static_cast<const Record *>(nullptr));

using TokenIndexes = std::map<Handle, size_t>; // N of __ysta_tok_N by object
using EpochHook = bool (*)();
template <typename Record> using ViewTableGetter = const std::deque<Record> &(NetView::*)() const;

namespace detail {

constexpr std::string_view token_prefix = "__ysta_tok_";
constexpr std::string_view stale_name = "__ysta_stale"; // net of an earlier network, see write_sdc_cmd in trace.tcl

using NameMap = std::map<std::string, Handle, std::less<>>;

std::string staName(std::string_view raw);
std::string staNetName(const Netlist::Net *net, std::string path = "");
std::string hierName(const Yosys::RTLIL::AttrObject *object);
void addName(NameMap &names, const std::string &name, Handle handle);

void reversePortRuns(Netlist::PinList &pins);
Yosys::RTLIL::PortDir portDir(const sta::PortDirection *direction);
sta::PortDirection *staDirection(Yosys::RTLIL::PortDir dir);

template <typename Record> constexpr uint32_t first_id = std::is_same_v<Record, Netlist::Instance> ? 0 : 1;

template <typename Map> typename Map::mapped_type valueOrEmpty(const Map &map, std::string_view key)
{
	auto it = map.find(key);
	if (it == map.end())
		return {};
	return it->second;
}

template <typename Seq> bool appendMatches(const NameMap &names, const sta::PatternMatch *pattern, Seq &matches)
{
	size_t count = matches.size();
	for (const auto &[name, handle] : names) {
		if (handle.bits != 0 && pattern->match(name))
			matches.push_back(handle);
	}
	return matches.size() != count;
}

template <typename T> class SeqIterator : public sta::Iterator<T> {
	std::vector<T> seq_;
	size_t index_ = 0;
public:
	template <typename Range> explicit SeqIterator(const Range &handles) : seq_(handles.begin(), handles.end()) {}
	bool hasNext() override { return index_ < seq_.size(); }
	T next() override { return seq_[index_++]; }
};

} // namespace detail

// errors go through report_->error, it throws sta::Exception
// token mode changes what write_sdc writes, see setTokens
class YosysNetwork : public sta::ConcreteNetwork, public Netlist::CellLibrary {
public:
	using sta::ConcreteNetwork::name;
	using sta::ConcreteNetwork::cell;
	using sta::ConcreteNetwork::direction;

	explicit YosysNetwork(EpochHook open_epoch_hook) : open_epoch_hook_(open_epoch_hook) {}

	size_t link(std::unique_ptr<NetView> view);
	void unlink();
	// when unlinked view() refuses, topInstance() is null, the tables are empty
	bool linked() const { return view_ != nullptr; }
	void dropHierNames() { hier_names_.clear(); } // a pass can change an hdlname, the view fingerprint has none
	NetView &view() const;
	bool describe(Netlist::CellType &type) override;
	bool isLinked() const override;

	// token mode while set, see Epochs::capture
	// non-top instances, pins, nets and scalar top ports get token names
	void setTokens(TokenIndexes *tokens) { tokens_ = tokens; }
	std::string token(Handle handle) const;
	// Tcl links ::ysta::writing_sdc to it, see names_stale_nets_
	int *staleNetNamesFlag() { return &names_stale_nets_; }

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

	sta::ObjectId id(const sta::Pin *pin) const override;
	sta::Instance *instance(const sta::Pin *pin) const override;
	sta::Net *net(const sta::Pin *pin) const override;
	sta::Term *term(const sta::Pin *pin) const override;
	sta::Port *port(const sta::Pin *pin) const override;
	sta::PortDirection *direction(const sta::Pin *pin) const override;
	sta::VertexId vertexId(const sta::Pin *pin) const override;
	void setVertexId(sta::Pin *pin, sta::VertexId id) override;
	bool isLeaf(const sta::Pin *pin) const override;
	std::string pathName(const sta::Pin *pin) const override;
	std::string name(const sta::Port *port) const override;

	sta::ObjectId id(const sta::Term *term) const override;
	sta::Net *net(const sta::Term *term) const override;
	sta::Pin *pin(const sta::Term *term) const override;

	std::string name(const sta::Net *net) const override;
	sta::ObjectId id(const sta::Net *net) const override;
	std::string pathName(const sta::Net *net) const override;
	sta::Net *findNet(const sta::Instance *instance, std::string_view net_name) const override;
	void findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern,
	                          sta::NetSeq &matches) const override;
	sta::Instance *instance(const sta::Net *net) const override;
	sta::NetPinIterator *pinIterator(const sta::Net *net) const override;
	sta::NetTermIterator *termIterator(const sta::Net *net) const override;
	bool isPower(const sta::Net *net) const override;
	bool isGround(const sta::Net *net) const override;

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
	// unlinked or another generation is an error
	template <typename Record>
	const Record *recordOf(Handle handle, ViewTableGetter<Record> table, Handle::Kind kind = record_kind<Record>) const
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
	struct StaCell {
		sta::ConcreteCell *cell = nullptr;
		std::vector<sta::ConcretePort *> bit_ports;                   // scalar or bus bits, in Instance::pins order
		Yosys::dict<const sta::ConcretePort *, uint32_t> pin_of_port; // position of a bit port
	};
	size_t buildTables(Netlist::Instance *scope);
	void addCell(const Netlist::Instance *inst);
	sta::ConcreteCell *concreteCell(const Netlist::Instance *inst);
	sta::ConcretePort *bitPort(sta::ConcreteCell *ccell, const Netlist::Port &shape, int bit) const;
	sta::ConcreteCell *makeShapedCell(const Netlist::Instance &inst);
	void refuseHandle() const;
	const Netlist::Instance *record(const sta::Instance *inst) const { return recordOf(inst, &NetView::instances); }
	const Netlist::Pin *record(const sta::Pin *pin) const { return recordOf(pin, &NetView::pins); }
	const Netlist::Pin *record(const sta::Term *term) const
	{
		return recordOf(term, &NetView::pins, Handle::Kind::Term);
	}
	const Netlist::Net *record(const sta::Net *net) const { return recordOf(net, &NetView::nets); }
	std::unique_ptr<NetView> view_;
	uint32_t generation_ = 1;                                         // link() bumps, unlink() keeps, old handles fail
	TokenIndexes *tokens_ = nullptr;
	int names_stale_nets_ = 0;                                        // set by write_sdc, old nets get the stale name
	sta::Library *design_lib_ = nullptr;
	EpochHook open_epoch_hook_;                                       // queryHook, relinks a changed design
	sta::AttributeMap no_attributes_;
	std::vector<std::unique_ptr<sta::ConcreteLibrary>> retired_libs_; // of earlier links, OpenSTA can point in
	Yosys::dict<const Netlist::CellType *, StaCell> cells_;
	std::map<std::string, sta::ConcreteCell *> cells_by_key_;         // see concreteCell
	std::vector<sta::VertexId> pin_vertex_ids_;                       // by pin id, in the graph of this link

	struct ScopeNames {
		detail::NameMap children;
		detail::NameMap nets;
	};
	std::vector<ScopeNames> scope_names_;               // sta names per scope, by instance id
	const ScopeNames &scopeNames(const sta::Instance *instance) const;
	mutable std::map<uint32_t, ScopeNames> hier_names_; // hdlname paths by instance id, see hierNames
	const ScopeNames &hierNames(const sta::Instance *instance, std::string_view name) const;
};

} // namespace ysta

#endif
