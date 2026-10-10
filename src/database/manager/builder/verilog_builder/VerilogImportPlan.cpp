// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogImportPlan.hh"

#include <algorithm>
#include <numeric>
#include <unordered_set>

#include "VerilogConstantNet.hh"
#include "VerilogLibrary.hh"
#include "verilog/VerilogValue.hh"
namespace idb::verilog_import {
namespace {
using namespace verilog;
using detail::Error;
class PlanBuilder
{
 public:
  PlanBuilder(const Netlist& flat, VerilogLibrary& library) : _flat(flat), _library(library) {}
  VerilogImportPlan run()
  {
    _parent.resize(_flat.nets.size());
    std::iota(_parent.begin(), _parent.end(), NetId(0));
    _representative = _parent;
    _rank.resize(_parent.size());
    _assignment_targets.resize(_parent.size());
    for (NetId i = 0; i < _flat.nets.size(); ++i) {
      const auto type = _flat.nets[i].type;
      if (type == NetType::supply0 || type == NetType::supply1)
        _representative[i] = type == NetType::supply0 ? zeroBit : oneBit;
    }
    resolveAliases();
    _canonical.resize(_parent.size());
    for (NetId i = 0; i < _parent.size(); ++i)
      _canonical[i] = _representative[root(i)];
    validate();
    _plan.top = _flat.top;
    _mapped.resize(_flat.nets.size(), xBit);
    for (NetId i = 0; i < _flat.nets.size(); ++i) {
      if (_canonical[i] != i)
        continue;
      _mapped[i] = _plan.nets.size();
      _plan.nets.push_back({_flat.nets[i].name, NetType::wire});
    }
    std::unordered_set<std::string> io_buses;
    for (const auto& port : _flat.ports) {
      PlannedBus bus;
      if (port.range) {
        bus = {port.name, unsigned(port.range->first), unsigned(port.range->second), {}};
        io_buses.insert(port.name);
      }
      for (size_t bit = 0; bit < port.signal.bits.size(); ++bit) {
        const auto index
            = port.range ? int64_t(port.range->first) + (port.range->first <= port.range->second ? int64_t(bit) : -int64_t(bit)) : 0;
        auto name = port.range ? port.name + "[" + std::to_string(index) + "]" : port.name;
        if (port.range)
          bus.members.push_back({unsigned(index), uint32_t(_plan.ports.size())});
        _plan.ports.push_back({std::move(name), mapped(port.signal.bits[bit]), port.direction});
      }
      if (port.range)
        _plan.port_buses.push_back(std::move(bus));
    }
    for (const auto& declaration : _flat.buses) {
      if (io_buses.count(declaration.name))
        continue;
      PlannedBus bus{declaration.name, unsigned(declaration.left), unsigned(declaration.right), {}};
      for (size_t bit = 0; bit < declaration.bits.size(); ++bit) {
        const auto index = int64_t(declaration.left) + (declaration.left <= declaration.right ? int64_t(bit) : -int64_t(bit));
        bus.members.push_back({unsigned(index), mapped(declaration.bits[bit])});
      }
      _plan.net_buses.push_back(std::move(bus));
    }
    _plan.instances.reserve(_flat.instances.size());
    for (const auto& cell : _flat.instances) {
      PlannedInstance instance{cell.name, cell.type, {}, {}};
      const auto& interface = *_library.find(cell.type);
      for (const auto& connection : cell.ports) {
        if (connection.signal.bits.empty())
          continue;
        const auto& port = interface.ports.at(connection.name);
        const auto value = resized(connection.signal, port.pins.size());
        PlannedBus bus;
        if (port.range)
          bus = {cell.name + "/" + connection.name, unsigned(port.range->first), unsigned(port.range->second), {}};
        for (size_t bit = 0; bit < port.pins.size(); ++bit) {
          if (port.range)
            bus.members.push_back({unsigned(port.range->first) - unsigned(bit), uint32_t(instance.pins.size())});
          instance.pins.push_back({port.pins[bit], mapped(value.bits[bit]), port.direction});
        }
        if (port.range)
          instance.buses.push_back(std::move(bus));
      }
      _plan.instances.push_back(std::move(instance));
    }
    return std::move(_plan);
  }

 private:
  [[noreturn]] void fail(SourceLocation location, const std::string& message) const { throw Error(location, message); }
  NetId mapped(NetId bit)
  {
    bit = canonical(bit);
    if (!isConstant(bit))
      return _mapped.at(bit);
    auto& id = bit == oneBit ? _one : _zero;
    if (id == xBit) {
      id = _plan.nets.size();
      _plan.nets.push_back(
          {std::string(bit == oneBit ? verilogOneNet : verilogZeroNet), bit == oneBit ? NetType::supply1 : NetType::supply0});
    }
    return id;
  }
  NetId root(NetId id)
  {
    NetId result = id;
    while (_parent[result] != result)
      result = _parent[result];
    while (_parent[id] != id) {
      const auto next = _parent[id];
      _parent[id] = result;
      id = next;
    }
    return result;
  }
  void join(NetId a, NetId b, SourceLocation location)
  {
    const auto ra = isConstant(a) ? a : root(a);
    const auto rb = isConstant(b) ? b : root(b);
    if (ra == rb)
      return;
    const auto va = isConstant(a) ? a : _representative[ra];
    const auto vb = isConstant(b) ? b : _representative[rb];
    if (isConstant(va) && isConstant(vb) && va != vb)
      throw Error(location, "conflicting constant drivers cannot be represented as a physical net");
    const auto value = isConstant(va) ? va : isConstant(vb) ? vb : std::min(va, vb);
    if (isConstant(a)) {
      _representative[rb] = value;
      return;
    }
    if (isConstant(b)) {
      _representative[ra] = value;
      return;
    }
    NetId parent = ra, child = rb;
    if (_rank[parent] < _rank[child])
      std::swap(parent, child);
    if (_rank[parent] == _rank[child])
      ++_rank[parent];
    _parent[child] = parent;
    _representative[parent] = value;
  }
  void resolveAliases()
  {
    // Inout ports are electrical connections; continuous assignments and input/output
    // bindings are directed drivers. Validate driver provenance before collapsing them.
    for (const auto& edge : _flat.inouts)
      join(edge.target, edge.source, edge.location);
    std::vector<bool> driven(_flat.nets.size(), false);
    for (const auto& edge : _flat.assignments) {
      const auto target = root(edge.target);
      if (!isConstant(edge.source) && target == root(edge.source))
        continue;
      if (driven[target] || isConstant(_representative[target]))
        throw Error(edge.location, "multiple or supply assignment drivers cannot be collapsed into a physical net");
      driven[target] = true;
    }
    for (NetId bit = 0; bit < _flat.nets.size(); ++bit)
      _assignment_targets[bit] = driven[root(bit)];
    for (const auto& port : _flat.ports)
      if (port.direction == Direction::input || port.direction == Direction::inout)
        for (auto bit : port.signal.bits)
          if (driven[root(bit)] || isConstant(_representative[root(bit)]))
            throw Error({}, "top input/inout also has an internal driver; cannot collapse directed drivers");
    for (const auto& edge : _flat.assignments)
      join(edge.target, edge.source, edge.location);
  }
  NetId canonical(NetId bit) const { return isConstant(bit) ? bit : _canonical.at(bit); }
  void validateBit(NetId bit, SourceLocation location)
  {
    bit = canonical(bit);
    if (bit == xBit || bit == zBit)
      fail(location, "four-state x/z connection cannot be represented by the physical database");
  }
  void validate()
  {
    for (const auto& net : _flat.nets) {
      if ((net.name == verilogZeroNet && net.type != NetType::supply0) || (net.name == verilogOneNet && net.type != NetType::supply1))
        fail({}, "net name conflicts with the importer's constant-net convention: " + net.name);
      if (net.type == NetType::wand || net.type == NetType::wor)
        fail({}, "wired logic resolution cannot be represented by the physical database: " + net.name);
    }
    for (const auto& bus : _flat.buses)
      if (bus.left < 0 || bus.right < 0 || bus.left > int(BitVector::kMaxWidth) || bus.right > int(BitVector::kMaxWidth))
        fail({}, "bus index cannot be represented by the physical database: " + bus.name);
    for (const auto& port : _flat.ports)
      for (auto bit : port.signal.bits)
        validateBit(bit, {});
    for (const auto& bus : _flat.buses)
      for (auto bit : bus.bits)
        validateBit(bit, {});
    for (const auto& instance : _flat.instances) {
      const auto* interface = _library.find(instance.type);
      if (!interface)
        fail(instance.location, "PDK master not found: instance=" + instance.name + ", master=" + instance.type);
      std::unordered_set<std::string> connected_pins;
      for (const auto& connection : instance.ports) {
        if (connection.name.empty())
          fail(connection.location, "positional connections to a LEF-only cell lack a Verilog port order: instance=" + instance.name);
        const auto port = interface->ports.find(connection.name);
        if (port == interface->ports.end())
          fail(connection.location,
               "LEF pin not found: instance=" + instance.name + ", master=" + instance.type + ", pin=" + connection.name);
        if (interface->has_buses)
          for (const auto& pin : port->second.pins)
            if (!connected_pins.insert(pin).second)
              fail(connection.location, "overlapping LEF port mappings: instance=" + instance.name + ", pin=" + pin);
        if (connection.signal.bits.empty())
          continue;
        if (port->second.direction != Direction::input && connection.signal.bits.size() != port->second.pins.size())
          fail(connection.location,
               "output/inout width mismatch for LEF-only cell: instance=" + instance.name + ", pin=" + connection.name);
        for (auto bit : resized(connection.signal, port->second.pins.size()).bits) {
          validateBit(bit, connection.location);
          if ((port->second.direction == Direction::output || port->second.direction == Direction::inout) && !isConstant(bit)
              && (_assignment_targets[bit] || isConstant(canonical(bit))))
            fail(connection.location, "cell output and assignment both drive the same net; cannot collapse drivers: instance="
                                          + instance.name + ", pin=" + connection.name);
          if ((port->second.direction == Direction::output || port->second.direction == Direction::inout) && isConstant(bit))
            fail(connection.location,
                 "output/inout connection must be a net lvalue: instance=" + instance.name + ", pin=" + connection.name);
        }
      }
    }
  }

  const Netlist& _flat;
  VerilogLibrary& _library;
  VerilogImportPlan _plan;
  std::vector<NetId> _parent, _representative, _canonical, _mapped;
  std::vector<uint8_t> _rank;
  std::vector<bool> _assignment_targets;
  NetId _zero = xBit, _one = xBit;
};
}  // namespace
ImportPlanResult makeImportPlan(const verilog::Netlist& flat, VerilogLibrary& library)
{
  ImportPlanResult result;
  try {
    result.plan = std::make_unique<VerilogImportPlan>(PlanBuilder(flat, library).run());
  } catch (const verilog::detail::Error& error) {
    result.diagnostics.push_back({error.location.file < flat.source_files.size() ? flat.source_files[error.location.file] : flat.source,
                                  error.location, error.what()});
  }
  return result;
}
}  // namespace idb::verilog_import
