#ifndef YOSYS_NETWORK_HH
#define YOSYS_NETWORK_HH

#include "kernel/netview.h"

#include "sta/ConcreteNetwork.hh"

#include <map>
#include <set>
#include <unordered_map>

namespace ysta {

using Netlist = Yosys::Netlist;
using YInstance = Yosys::Netlist::Instance;
using YPin = Yosys::Netlist::Pin;
using YNet = Yosys::Netlist::Net;
using YTerm = Yosys::Netlist::Term;

namespace detail {

constexpr std::string_view token_prefix = "__ysta_tok_";

// Names
std::string staName(std::string_view raw);
std::string staPath(const std::vector<std::string> &path);
std::string staInstName(const Netlist::Instance *inst);
std::string staWireName(const Netlist::NetName &name);
std::string staNetName(const Yosys::NetView &netlist, const Netlist::Net *net);
sta::PortDirection *staDirection(Netlist::Dir dir);

// Order of ConcreteNetwork
void reversePortRuns(std::vector<Netlist::Pin *> &pins);

// Scope body keys
std::string pinKey(const Yosys::NetView &netlist, const Netlist::Pin *pin);
std::string netKey(const Yosys::NetView &netlist, const Netlist::Net *net);

enum class ClaimKind : uint8_t {
	Canonical,
	Alias
};

// Claim of an STA net name by the source bit that gives it. Canonical names
// beat aliases, then the smallest bit wins
struct NameClaim {
	Netlist::Net *net = nullptr;
	ClaimKind kind = ClaimKind::Canonical;
	std::string wire;
	int index = 0;
	bool operator<(const NameClaim &other) const;
};
using NetNames = std::map<std::string, NameClaim, std::less<>>;
using ChildNames = std::map<std::string, Netlist::Instance *, std::less<>>;
using PinNames = std::map<std::string, Netlist::Pin *, std::less<>>;
NameClaim netClaim(const Yosys::NetView &netlist, Netlist::Net *net);
void addClaim(NetNames &names, const std::string &name, const NameClaim &claim);
size_t scopeNetNames(const Yosys::NetView &netlist, const Netlist::Instance *scope, NetNames &names);
std::vector<std::string> childNames(const Netlist::Instance *scope, std::vector<std::string> &own);
void scopeChildNames(const Netlist::Instance *scope, const std::vector<std::string> &names,
                     const std::vector<std::string> &own, ChildNames &children);

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

enum class EpochPhase : uint8_t {
	Open,
	Closed
};

enum class StaCellKind : uint8_t {
	Leaf,
	Hierarchical
};

// a Port token holds the sta::Port, not its pin
struct Token {
	using ObjectKind = Yosys::TrackedRef::ObjectKind;
	ObjectKind kind;
	const void *object;
};

class YosysNetwork : public sta::ConcreteNetwork, public Netlist::LeafCellModel {
public:
	using sta::ConcreteNetwork::name;
	using sta::ConcreteNetwork::cell;
	using sta::ConcreteNetwork::direction;

	void attach(Yosys::NetView *view);
	void link();
	void unlink();
	bool linked() const;
	size_t takeSharedNames();

	bool ports(const YInstance &inst, std::vector<Netlist::PortShape> &out) override;
	bool isRegister(const YInstance &inst) override;
	bool isClockPin(const YPin &pin) override;
	bool isBuffer(const YInstance &inst) override;

	bool linkNetwork(std::string_view top_cell_name, bool make_black_boxes, sta::Report *report) override;
	bool isLinked() const override;
	void setEpochPhase(EpochPhase phase);
	bool epochClosed() const;
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
	std::string token(Token::ObjectKind kind, const void *object) const;
	sta::Instance *findChild(const sta::Instance *parent, std::string_view name) const override;
	sta::Pin *findPin(const sta::Instance *instance, std::string_view port_name) const override;
	sta::Pin *findPin(const sta::Instance *instance, const sta::Port *port) const override;
	sta::Pin *findPin(const sta::Instance *instance, const sta::LibertyPort *port) const override;
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
protected:
	void visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const override;
private:
	sta::ConcreteCell *typeCell(const YInstance &inst);
	sta::ConcreteCell *makeShapedCell(const std::string &name, StaCellKind kind, const std::vector<Netlist::PortShape> &shapes);
	sta::ConcretePort *portOf(const YPin *pin) const;
	void buildTables(YInstance *scope);
	sta::ConcreteCell *scopeCell(const YInstance *scope);
	std::string scopeBody(const YInstance *scope) const;
	void childLines(const YInstance *child, std::vector<std::string> &lines) const;
	std::string freeCellName(const YInstance *scope) const;
	void countMasters(const YInstance *scope);
	void indexPins(YInstance *inst);
	const std::string &instName(const YInstance *inst) const;
	void checkOwner(const YInstance *inst) const;
	const YInstance *asInst(const sta::Instance *inst) const;
	const YPin *asPin(const sta::Pin *pin) const;
	const YNet *asNet(const sta::Net *net) const;
	const YTerm *asTerm(const sta::Term *term) const;
private:
	Yosys::NetView *view_ = nullptr;
	sta::Library *design_lib_ = nullptr;
	sta::ConcreteCell *top_cell_ = nullptr;
	EpochPhase epoch_phase_ = EpochPhase::Open;
	size_t shared_name_count_ = 0; // nets with the canonical name of another net
	std::vector<Token> *token_sink_ = nullptr; // token mode when set
	mutable std::unordered_map<const void *, size_t> token_index_;
	void (*open_epoch_hook_)() = nullptr;

	// Cells and ports
	std::map<std::string, sta::ConcreteCell *, std::less<>> type_cells_;
	Yosys::IdMap<sta::ConcreteCell *> inst_cells_;
	Yosys::IdMap<sta::ConcretePort *> pin_ports_; // scalar or bus bit

	// Names, by instance id
	Yosys::IdMap<detail::ChildNames> child_names_;
	Yosys::IdMap<std::string> inst_names_;
	Yosys::IdMap<detail::NetNames> net_names_;
	Yosys::IdMap<detail::PinNames> pin_names_; // "D", "A[3]"

	// Link time: hierarchical instances per master, scope cells by master and body
	std::map<std::string, int, std::less<>> master_uses_;
	std::map<std::string, sta::ConcreteCell *, std::less<>> scope_cells_;
};

} // namespace ysta

#endif
