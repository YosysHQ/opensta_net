#ifndef YOSYS_NETWORK_HH
#define YOSYS_NETWORK_HH

#include "kernel/netlist.h"

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

// Names
std::string staName(std::string_view raw);
std::string staUnescape(std::string_view name);
std::string staPath(const std::vector<std::string> &path);
std::string staInstName(const Netlist::Instance *inst);
std::string staWireName(const Netlist::NetName &name);
std::string staNetName(const Netlist &netlist, const Netlist::Net *net);
sta::PortDirection *staDirection(Netlist::Dir dir);
Netlist::Dir netlistDir(const sta::PortDirection *dir);

// Order of ConcreteNetwork
void reversePortRuns(std::vector<Netlist::Pin *> &pins);

// Claim of an STA net name by the source bit that gives it. Canonical names
// beat aliases, then the smallest bit wins
struct NameClaim {
	Netlist::Net *net = nullptr;
	bool alias = false;
	std::string wire;
	int index = 0;
	bool operator<(const NameClaim &other) const;
};
using NetNames = std::map<std::string, NameClaim, std::less<>>;
using ChildNames = std::map<std::string, Netlist::Instance *, std::less<>>;
using PinNames = std::map<std::string, Netlist::Pin *, std::less<>>;
using PortNames = std::map<std::string, sta::ConcretePort *, std::less<>>;
NameClaim netClaim(const Netlist &netlist, Netlist::Net *net);
void addClaim(NetNames &names, const std::string &name, const NameClaim &claim);
void scopeNetNames(const Netlist &netlist, const Netlist::Instance *scope, NetNames &names);
void scopeChildNames(const Netlist::Instance *scope, ChildNames &children);

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

class YosysNetwork : public sta::ConcreteNetwork, public Netlist::PortModel {
public:
	using sta::ConcreteNetwork::name;
	using sta::ConcreteNetwork::cell;
	using sta::ConcreteNetwork::direction;

	void attach(Netlist *netlist);
	void link();
	void unlink();
	bool linked() const;

	bool ports(const YInstance &inst, std::vector<Netlist::PortShape> &out) override;

	bool linkNetwork(std::string_view top_cell_name, bool make_black_boxes, sta::Report *report) override;
	bool isLinked() const override;

	// Instance
	sta::Instance *topInstance() const override;
	std::string name(const sta::Instance *instance) const override;
	sta::ObjectId id(const sta::Instance *instance) const override;
	std::string getAttribute(const sta::Instance *inst, std::string_view key) const override;
	const sta::AttributeMap &attributeMap(const sta::Instance *inst) const override;
	sta::Cell *cell(const sta::Instance *instance) const override;
	sta::Instance *parent(const sta::Instance *instance) const override;
	bool isLeaf(const sta::Instance *instance) const override;
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
protected:
	void visitConnectedPins(const sta::Net *net, sta::PinVisitor &visitor, sta::NetSet &visited_nets) const override;
private:
	sta::ConcreteCell *typeCell(const YInstance &inst);
	sta::ConcreteCell *makeShapedCell(const std::string &name, bool leaf, const std::vector<Netlist::PortShape> &shapes);
	void indexPorts(sta::ConcreteCell *ccell);
	sta::ConcretePort *portOf(const YPin *pin) const;
	void buildTables(YInstance *scope);
	void indexPins(YInstance *inst);
	static const YInstance *asInst(const sta::Instance *inst);
	static const YPin *asPin(const sta::Pin *pin);
	static const YNet *asNet(const sta::Net *net);
	static const YTerm *asTerm(const sta::Term *term);
private:
	Netlist *netlist_ = nullptr;
	sta::Library *design_lib_ = nullptr;
	sta::ConcreteCell *top_cell_ = nullptr;
	std::vector<YInstance *> scopes_; // top and hierarchical instances

	// Cells and ports
	std::map<std::string, sta::ConcreteCell *, std::less<>> type_cells_;
	std::unordered_map<const sta::ConcreteCell *, detail::PortNames> cell_ports_; // by unescaped name
	Yosys::IdMap<sta::ConcreteCell *> inst_cells_;
	Yosys::IdMap<sta::ConcretePort *> pin_ports_; // scalar or bus bit
	Yosys::IdMap<sta::VertexId> vertex_ids_;      // by pin id

	// Names, by instance id
	Yosys::IdMap<detail::ChildNames> child_names_;
	Yosys::IdMap<detail::NetNames> net_names_;
	Yosys::IdMap<detail::PinNames> pin_names_; // "D", "A[3]"
};

} // namespace ysta

#endif
