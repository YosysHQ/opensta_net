#ifndef YOSYS_NETWORK_HH
#define YOSYS_NETWORK_HH

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

// Names
std::string staName(std::string_view raw);
std::string staPath(const std::vector<std::string> &path);
std::string staWireName(const Netlist::NetName &name);
std::string staNetName(const Yosys::NetView &netlist, const Netlist::Net *net);
sta::PortDirection *staDirection(Netlist::PortDir dir);

// Order of ConcreteNetwork
void reversePortRuns(std::vector<Netlist::Pin *> &pins);

// The net that an STA net name finds
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

// Map value, or an empty one
template <typename Map, typename K> const typename Map::mapped_type &lookup(const Map &map, const K &key)
{
	static const typename Map::mapped_type none{};
	auto it = map.find(key);
	if (it == map.end())
		return none;
	return it->second;
}

} // namespace detail

// The sta object types a handle stands for
template <typename T>
concept StaObject = 
	std::is_same_v<std::remove_const_t<T>, sta::Instance> || 
	std::is_same_v<std::remove_const_t<T>, sta::Pin> ||
	std::is_same_v<std::remove_const_t<T>, sta::Net> ||
	std::is_same_v<std::remove_const_t<T>, sta::Term>;

struct Handle {
	enum class Kind : uint8_t { Instance, Pin, Net, Term };
	static constexpr uint32_t generation_mask = (1u << 29) - 1;

	// 63    1
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
	std::string swigName() const; // the swig object name in tcl
	auto operator<=>(const Handle &) const = default;
};

constexpr Handle::Kind recordKind(const YInstance *) { return Handle::Kind::Instance; }
constexpr Handle::Kind recordKind(const YPin *) { return Handle::Kind::Pin; }
constexpr Handle::Kind recordKind(const YNet *) { return Handle::Kind::Net; }
template <typename Record> constexpr Handle::Kind record_kind = recordKind(static_cast<const Record *>(nullptr));

// a TopPort token holds the top pin
struct Token {
	using Kind = Yosys::TrackedRef::Kind;
	Kind kind;
	Handle handle;
	auto operator<=>(const Token &) const = default;
};

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

template <typename T> sta::Iterator<T> *handleIterator(const std::vector<Handle> &handles)
{
	return new SeqIterator<T>(std::vector<T>(handles.begin(), handles.end()));
}

} // namespace detail

class YosysNetwork : public sta::ConcreteNetwork, public Netlist::CellLibrary {
public:
	bool epoch_closed = false; // queries wait for the sync

	using sta::ConcreteNetwork::name;
	using sta::ConcreteNetwork::cell;
	using sta::ConcreteNetwork::direction;

	void attach(Yosys::NetView *view);
	void link();
	void unlink();
	bool linked() const;
	size_t takeSharedNames();

	bool ports(const YInstance &inst, std::vector<Netlist::Port> &out) override;
	bool isRegister(const YInstance &inst) override;
	bool isClockPin(const YPin &pin) override;
	bool isBuffer(const YInstance &inst) override;

	bool linkNetwork(std::string_view top_cell_name, bool make_black_boxes, sta::Report *report) override;
	bool isLinked() const override;
	void setOpenEpochHook(void (*hook)());

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
	void setTokenSink(std::vector<Token> *sink);
	// TODO: write_sdc names the nets of earlier links stale_name: OpenSTA keeps their resistances and voltages
	void setStaleNames(bool on);
	bool staleNet(const sta::Net *net) const;
	std::string token(Token::Kind kind, Handle handle) const;
	sta::Instance *findChild(const sta::Instance *parent, std::string_view name) const override;
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
	sta::Net *findNet(const sta::Instance *instance, std::string_view net_name) const override;
	void findInstNetsMatching(const sta::Instance *instance, const sta::PatternMatch *pattern, sta::NetSeq &matches) const override;
	sta::Instance *instance(const sta::Net *net) const override;
	sta::NetPinIterator *pinIterator(const sta::Net *net) const override;
	sta::NetTermIterator *termIterator(const sta::Net *net) const override;

	// Constant nets
	bool isPower(const sta::Net *net) const override;
	bool isGround(const sta::Net *net) const override;
	sta::ConstantPinIterator *constantPinIterator() override;

	void setAttribute(sta::Instance *instance, std::string_view key, std::string_view value) override;
	void readNetlistBefore() override;
	void clear() override;
	void refuseEdit(const char *what) const;

	// Records
	// a term is the pin, by Kind::Term
	template <typename Record> Handle handle(const Record *record, Handle::Kind kind = record_kind<Record>) const
	{
		if (record == nullptr)
			return {};
		return {kind, generation_, record->id};
	}
	template <typename Record> std::vector<Handle> handles(const std::vector<Record *> &records, Handle::Kind kind = record_kind<Record>) const
	{
		std::vector<Handle> result;
		for (const Record *record : records)
			result.push_back(handle(record, kind));
		return result;
	}
	template <typename Record> const Record *recordOf(Handle handle, Handle::Kind kind = record_kind<Record>) const
	{
		if (handle.bits == 0)
			return nullptr;
		const Record *record;
		if constexpr (std::is_same_v<Record, YInstance>)
			record = view_->instanceById(handle.id());
		else if constexpr (std::is_same_v<Record, YPin>)
			record = view_->pinById(handle.id());
		else
			record = view_->netById(handle.id());
		if (record == nullptr || handle.kind() != kind || handle.generation() != generation_)
			refuseHandle();
		return record;
	}
protected:
	void visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const override;
private:
	sta::ConcreteCell *typeCell(const YInstance &inst);
	sta::ConcreteCell *makeShapedCell(const std::string &name, bool leaf, const std::vector<Netlist::Port> &shapes);
	void retireLibrary(sta::Library *library);
	sta::ConcretePort *portOf(const YPin *pin) const;
	void buildTables(YInstance *scope);
	sta::ConcreteCell *scopeCell(const YInstance *scope);
	std::string freeCellName(const YInstance *scope) const;
	void countMasters(const YInstance *scope);
	void indexPins(YInstance *inst);
	void refuseHandle() const;
private:
	Yosys::NetView *view_ = nullptr;
	uint32_t generation_ = 1;
	sta::Library *design_lib_ = nullptr;
	sta::ConcreteCell *top_cell_ = nullptr;
	size_t shared_name_count_ = 0; // nets with the canonical name of another net
	std::vector<Token> *token_sink_ = nullptr; // token mode when set
	mutable std::map<Token, size_t> token_index_;
	bool stale_names_ = false;
	void (*open_epoch_hook_)() = nullptr;
	std::vector<std::unique_ptr<sta::ConcreteLibrary>> retired_libs_;

	// Cells and ports
	std::map<std::string, sta::ConcreteCell *, std::less<>> type_cells_;
	Netlist::IdMap<sta::ConcreteCell *> inst_cells_;
	Netlist::IdMap<sta::ConcretePort *> pin_ports_; // scalar or bus bit
	std::map<std::pair<uint32_t, const sta::ConcretePort *>, YPin *> port_pins_; // by instance id and port bit
	Netlist::IdMap<sta::VertexId> pin_vertex_ids_; // in the graph of this link

	// Names, by instance id
	Netlist::IdMap<detail::ChildNames> child_names_;
	Netlist::IdMap<detail::NetNames> net_names_;

	// Link time: hierarchical instances per master, scope cells by master and body
	std::map<std::string, int, std::less<>> master_uses_;
	std::map<std::string, sta::ConcreteCell *, std::less<>> scope_cells_;
};

} // namespace ysta

#endif
