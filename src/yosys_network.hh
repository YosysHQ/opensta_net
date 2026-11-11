#ifndef YOSYS_NETWORK_HH
#define YOSYS_NETWORK_HH

// YosysNetwork shows one Yosys::NetView to OpenSTA

#include "kernel/nettrack.h"

#include "sta/ConcreteLibrary.hh"
#include "sta/ConcreteNetwork.hh"

#include <cstdint>
#include <map>
#include <memory>
#include <set>

namespace ysta {

namespace Netlist = Yosys::Netlist;
using YInstance = Yosys::Netlist::Instance;
using YPin = Yosys::Netlist::Pin;
using YNet = Yosys::Netlist::Net;

namespace detail {

constexpr std::string_view token_prefix = "__ysta_tok_";
constexpr std::string_view stale_name = "\x01ysta_stale";

// names
std::string staName(std::string_view raw);
std::string staPath(const std::vector<std::string> &path);
std::string staWireName(const Netlist::NetName &name);
std::string staNetName(const Yosys::NetView &netlist, const Netlist::Net *net);
sta::PortDirection *staDirection(Netlist::PortDir dir);

// pin order as ConcreteNetwork gives it
void reversePortRuns(std::vector<Netlist::Pin *> &pins);

// the net that an STA net name finds
struct NameClaim {
	Netlist::Net *net = nullptr;
	bool canonical = false; // the name of the net, else an alias
};
using NetNames = std::map<std::string, NameClaim, std::less<>>;
struct RankedClaim {
	NameClaim claim;
	std::string wire; // with index, orders the claims of one canonicity
	int index;
};
using RankedClaims = std::map<std::string, RankedClaim>;
using ChildNames = std::map<std::string, Netlist::Instance *, std::less<>>;
void claimName(RankedClaims &claims, const Netlist::NetName &name, Netlist::Net *net, bool canonical);
size_t scopeNetNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, NetNames &names);

using ScopeCells = std::map<std::string, sta::ConcreteCell *, std::less<>>;

// the map value, or an empty one
template <typename Map, typename K> const typename Map::mapped_type &lookup(const Map &map, const K &key)
{
	static const typename Map::mapped_type none{};
	auto it = map.find(key);
	if (it == map.end())
		return none;
	return it->second;
}

} // namespace detail

// the sta object types a handle stands for
template <typename T>
concept StaObject = 
	std::is_same_v<std::remove_const_t<T>, sta::Instance> ||
	std::is_same_v<std::remove_const_t<T>, sta::Pin> ||
	std::is_same_v<std::remove_const_t<T>, sta::Net> ||
	std::is_same_v<std::remove_const_t<T>, sta::Term>;

// a view record id packed into an sta object pointer, see kernel/netview.h
// OpenSTA gets only Handles, never record pointers
struct Handle {
	enum class Kind : uint8_t { Instance, Pin, Net, Term };
	static constexpr uint32_t generation_mask = (1u << 29) - 1;

	// 63    handle? (1 => handle)
	// 61-62 kind
	// 32-60 generation
	// 0-31  record id
	uint64_t bits = 0;
public:
	Handle() = default;
	Handle(Kind kind, uint32_t generation, uint32_t id);
	template <StaObject T> Handle(const T *object) : bits(reinterpret_cast<uintptr_t>(object)) {}
	template <StaObject T> operator T *() const { return reinterpret_cast<T *>(bits); }
	Kind kind() const;
	uint32_t generation() const;
	uint32_t id() const;
	std::string swigName() const; // the SWIG object name in Tcl
	auto operator<=>(const Handle &) const = default;
};

constexpr Handle::Kind recordKind(const YInstance *) { return Handle::Kind::Instance; }
constexpr Handle::Kind recordKind(const YPin *) { return Handle::Kind::Pin; }
constexpr Handle::Kind recordKind(const YNet *) { return Handle::Kind::Net; }
template <typename Record> constexpr Handle::Kind record_kind = recordKind(static_cast<const Record *>(nullptr));

// an object written as __ysta_tok_N, N indexes the token list
// a TopPort token holds the top pin
struct Token {
	using Kind = Yosys::TrackedRef::Kind;
	Kind kind;
	Handle handle;
	auto operator<=>(const Token &) const = default;
};

namespace detail {

// iterator over a copied sequence
template <typename T> class SeqIterator : public sta::Iterator<T> {
public:
	explicit SeqIterator(std::vector<T> seq) : seq_(std::move(seq)) {}
	bool hasNext() override { return index_ < seq_.size(); }
	T next() override { return seq_[index_++]; }

private:
	std::vector<T> seq_;
	size_t index_ = 0;
};

template <typename T> sta::Iterator<T> *handleIterator(const std::vector<Handle> &handles)
{
	return new SeqIterator<T>(std::vector<T>(handles.begin(), handles.end()));
}

} // namespace detail

// errors go through report_->error, which throws sta::Exception
// token mode and stale names change what write_sdc writes
// callers of write_sdc set them, apart from the link
class YosysNetwork : public sta::ConcreteNetwork, public Netlist::CellLibrary {
public:
	using sta::ConcreteNetwork::name;
	using sta::ConcreteNetwork::cell;
	using sta::ConcreteNetwork::direction;

	// needs unlinked, builds cells, ports, name tables and a new handle generation
	// returns how many nets share a name, linkView warns
	size_t link(std::unique_ptr<Yosys::NetView> view);
	// drops the view and the tables
	// epoch.cc calls it before each link, after a throw in sync or link
	void unlink();
	// unlinked, top_cell_ and topInstance() are null, tables empty
	// linked, tables map records to cells and ports, and names to records
	bool linked() const;
	Yosys::NetView &view() const; // of the link

	// Netlist::CellLibrary from liberty cells
	// NetView asks ports while it builds, an unmapped internal cell errors
	// it asks the others on the linked view
	bool ports(const YInstance &inst, std::vector<Netlist::Port> &out) override;
	bool isRegister(const YInstance &inst) override;
	bool isClockPin(const YPin &pin) override;
	bool isBuffer(const YInstance &inst) override;

	bool linkNetwork(std::string_view top_cell_name, bool make_black_boxes, sta::Report *report) override;
	// OpenSTA asks before each network read, see Sta::ensureLinked
	bool isLinked() const override;
	// required, set once after construction, isLinked returns its result
	// queryHook -> Epochs::syncForQuery, relinks if the design changed
	void setOpenEpochHook(bool (*hook)());

	// Instance
	sta::Instance *topInstance() const override;
	std::string name(const sta::Instance *instance) const override;
	sta::ObjectId id(const sta::Instance *instance) const override;
	std::string getAttribute(const sta::Instance *inst, std::string_view key) const override;
	const sta::AttributeMap &attributeMap(const sta::Instance *inst) const override;
	sta::Cell *cell(const sta::Instance *instance) const override;
	sta::Instance *parent(const sta::Instance *instance) const override;
	bool isLeaf(const sta::Instance *instance) const override;
	// in token mode, so write_sdc writes a hierarchical -through pin as itself
	bool isLeaf(const sta::Pin *pin) const override;
	std::string pathName(const sta::Instance *instance) const override;
	std::string pathName(const sta::Pin *pin) const override;
	std::string pathName(const sta::Net *net) const override;
	std::string name(const sta::Port *port) const override;
	// token mode while sink is set, see Epochs::capture
	// non-top instances, pins, nets and scalar top ports get token names
	void setTokenSink(std::vector<Token> *sink);
	// stale names mode while on, set by ysta::stale_names around write_sdc
	// old generation nets are stale_name in top, trace.tcl drops those lines
	// TODO: OpenSTA keeps resistances and voltages of old nets, write_sdc names them stale_name
	void setStaleNames(bool on);
	bool staleNet(const sta::Net *net) const;
	std::string token(Token::Kind kind, Handle handle) const;
	sta::Instance *findChild(const sta::Instance *parent, std::string_view name) const override;
	void findChildrenMatching(const sta::Instance *parent, const sta::PatternMatch *pattern, sta::InstanceSeq &matches) const override;
	sta::Pin *findPin(const sta::Instance *instance, std::string_view port_name) const override;
	sta::Pin *findPin(const sta::Instance *instance, const sta::Port *port) const override;
	sta::InstanceChildIterator *childIterator(const sta::Instance *instance) const override;
	sta::InstancePinIterator *pinIterator(const sta::Instance *instance) const override;
	sta::InstanceNetIterator *netIterator(const sta::Instance *instance) const override;

	// Pin
	sta::ObjectId id(const sta::Pin *pin) const override;
	sta::Instance *instance(const sta::Pin *pin) const override;
	sta::Net *net(const sta::Pin *pin) const override;
	sta::Term *term(const sta::Pin *pin) const override;
	sta::Port *port(const sta::Pin *pin) const override;
	sta::PortDirection *direction(const sta::Pin *pin) const override;
	sta::VertexId vertexId(const sta::Pin *pin) const override;
	void setVertexId(sta::Pin *pin, sta::VertexId id) override;

	// Term
	sta::ObjectId id(const sta::Term *term) const override;
	sta::Net *net(const sta::Term *term) const override;
	sta::Pin *pin(const sta::Term *term) const override;

	// Net
	std::string name(const sta::Net *net) const override;
	sta::ObjectId id(const sta::Net *net) const override;
	// the claim winner in net_names_
	sta::Net *findNet(const sta::Instance *instance, std::string_view net_name) const override;
	void findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern, sta::NetSeq &matches) const override;
	sta::Instance *instance(const sta::Net *net) const override;
	sta::NetPinIterator *pinIterator(const sta::Net *net) const override;
	sta::NetTermIterator *termIterator(const sta::Net *net) const override;

	// constant nets
	bool isPower(const sta::Net *net) const override;
	bool isGround(const sta::Net *net) const override;
	sta::ConstantPinIterator *constantPinIterator() override;

	void setAttribute(sta::Instance *instance, std::string_view key, std::string_view value) override;
	void readNetlistBefore() override;
	void clear() override;

	// records
	// a Term is the inside of a scope pin, pin->inner
	// its handle is the pin id with Kind::Term
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
	// decodes a handle, unlinked or another generation is an error
	template <typename Record> const Record *recordOf(Handle handle, Handle::Kind kind = record_kind<Record>) const
	{
		if (handle.bits == 0)
			return nullptr;
		const Record *record;
		if constexpr (std::is_same_v<Record, YInstance>)
			record = view().instanceById(handle.id());
		else if constexpr (std::is_same_v<Record, YPin>)
			record = view().pinById(handle.id());
		else
			record = view().netById(handle.id());
		if (record == nullptr || handle.kind() != kind || handle.generation() != generation_)
			refuseHandle();
		return record;
	}
protected:
	void visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const override;
private:
	sta::ConcreteCell *typeCell(const YInstance &inst);
	sta::ConcreteCell *makeShapedCell(const std::string &name, const YInstance &inst);
	sta::ConcretePort *portOf(const YPin *pin) const;
	size_t buildTables(YInstance *scope, detail::ScopeCells &scope_cells);
	sta::ConcreteCell *scopeCell(const YInstance *scope, detail::ScopeCells &scope_cells);
	std::string freeCellName(const YInstance *scope) const;
	void indexPins(YInstance *inst);
	// error 2929, throws, the network stays
	void refuseEdit(const char *what) const;
	void refuseHandle() const;
private:
	// link and modes
	std::unique_ptr<Yosys::NetView> view_;
	uint32_t generation_ = 1; // link() bumps it, unlink() keeps it to refuse old handles
	sta::Library *design_lib_ = nullptr;
	sta::ConcreteCell *top_cell_ = nullptr;
	std::vector<Token> *token_sink_ = nullptr; // token mode when set
	mutable std::map<Token, size_t> token_index_;
	bool stale_names_ = false; // see setStaleNames
	bool (*open_epoch_hook_)() = nullptr;
	// libraries of earlier links, OpenSTA can still point into them
	std::vector<std::unique_ptr<sta::ConcreteLibrary>> retired_libs_;

	// cells and ports
	std::map<std::string, sta::ConcreteCell *, std::less<>> type_cells_;
	Netlist::IdMap<sta::ConcreteCell *> inst_cells_;
	Netlist::IdMap<sta::ConcretePort *> pin_ports_; // scalar or bus bit
	// by instance id and port bit
	std::map<std::pair<uint32_t, const sta::ConcretePort *>, YPin *> port_pins_;
	Netlist::IdMap<sta::VertexId> pin_vertex_ids_; // in the graph of this link

	// names by instance id
	Netlist::IdMap<detail::ChildNames> child_names_;
	Netlist::IdMap<detail::NetNames>   net_names_;
};

} // namespace ysta

#endif
