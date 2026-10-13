// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "VerilogNetlistImportService.h"

#include <charconv>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "connectivity/ConnectivityService.h"
#include "design/DesignStore.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "instance/InstanceService.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "verilog/VerilogFrontend.hh"
#include "verilog/VerilogNetlist.hh"
#include "verilog/VerilogTypes.hh"

namespace eccdb {
namespace {
using idb::verilog::NetId;

struct IndexedName { std::string base; int32_t index; };
std::optional<IndexedName> indexed(std::string_view name)
{
  const auto open = name.rfind('[');
  if (open == std::string_view::npos || name.empty() || name.back() != ']' || open == 0 || open + 2 >= name.size()) return std::nullopt;
  int32_t value = 0;
  const auto result = std::from_chars(name.data() + open + 1, name.data() + name.size() - 1, value);
  if (result.ec != std::errc{} || result.ptr != name.data() + name.size() - 1) return std::nullopt;
  return IndexedName{std::string(name.substr(0, open)), value};
}

idb::verilog::Direction toDir(LibraryMasterTermDirection value)
{
  switch (value) {
    case LibraryMasterTermDirection::kInput: return idb::verilog::Direction::input;
    case LibraryMasterTermDirection::kOutput: return idb::verilog::Direction::output;
    case LibraryMasterTermDirection::kInOut: return idb::verilog::Direction::inout;
    default: return idb::verilog::Direction::none;
  }
}
DesignIoPinDirection toIoDir(idb::verilog::Direction value)
{
  switch (value) {
    case idb::verilog::Direction::input: return DesignIoPinDirection::kInput;
    case idb::verilog::Direction::output: return DesignIoPinDirection::kOutput;
    case idb::verilog::Direction::inout: return DesignIoPinDirection::kInOut;
    default: return DesignIoPinDirection::kNone;
  }
}
DesignSignalUse toUse(idb::verilog::NetType value)
{
  return value == idb::verilog::NetType::supply0 ? DesignSignalUse::kGround
       : value == idb::verilog::NetType::supply1 ? DesignSignalUse::kPower : DesignSignalUse::kSignal;
}

class LibraryLookup
{
 public:
  explicit LibraryLookup(const LibraryStore& value) : _library(value) {}
  const idb::verilog::LibraryCell* operator()(std::string_view name)
  {
    const auto master = _library.cellMasterStorage().findCellMaster(name);
    if (!master) return nullptr;
    auto [it, inserted] = _cells.try_emplace(std::string(name));
    if (inserted) {
      const auto& cell = _library.cellMasterStorage().cellMaster(master);
      std::map<std::string, std::map<int32_t, idb::verilog::Direction>> buses;
      for (const auto term_id : cell.terms) {
        const auto& term = _library.masterTermStorage().masterTerm(term_id);
        const auto direction = toDir(term.direction);
        it->second.ports.emplace(term.name, idb::verilog::PortShape{direction, 1});
        if (const auto item = indexed(term.name)) buses[item->base][item->index] = direction;
      }
      for (const auto& [base, members] : buses) {
        if (it->second.ports.contains(base) || members.empty()) continue;
        const auto low = members.begin()->first;
        const auto high = members.rbegin()->first;
        if (high - low + 1 != static_cast<int32_t>(members.size())) continue;
        it->second.ports.emplace(base, idb::verilog::PortShape{members.begin()->second,
                                                               static_cast<uint32_t>(high - low + 1), false, false,
                                                               idb::verilog::NetType::wire, false, std::make_pair(high, low)});
      }
    }
    return &it->second;
  }
 private:
  const LibraryStore& _library;
  std::map<std::string, idb::verilog::LibraryCell> _cells;
};

class Aliases
{
 public:
  explicit Aliases(std::size_t count) : _parent(count), _rank(count), _constant(count, idb::verilog::xBit)
  { std::iota(_parent.begin(), _parent.end(), NetId(0)); }
  void supply(NetId id, idb::verilog::NetType type)
  { if (type == idb::verilog::NetType::supply0) _constant[id] = idb::verilog::zeroBit;
    if (type == idb::verilog::NetType::supply1) _constant[id] = idb::verilog::oneBit; }
  NetId root(NetId id)
  {
    if (idb::verilog::isConstant(id)) return id;
    auto r = id; while (_parent[r] != r) r = _parent[r];
    while (_parent[id] != id) { auto next = _parent[id]; _parent[id] = r; id = next; }
    return r;
  }
  NetId canonical(NetId id) { if (idb::verilog::isConstant(id)) return id; const auto r = root(id); return _constant[r] == idb::verilog::xBit ? r : _constant[r]; }
  void join(NetId a, NetId b, const idb::verilog::SourceLocation& loc)
  {
    if (idb::verilog::isConstant(a) || idb::verilog::isConstant(b)) {
      const auto constant = idb::verilog::isConstant(a) ? a : b;
      if (constant == idb::verilog::xBit || constant == idb::verilog::zBit) throw idb::verilog::detail::Error(loc, "X/Z net cannot be connected");
      const auto net = idb::verilog::isConstant(a) ? b : a;
      const auto r = root(net);
      if (_constant[r] != idb::verilog::xBit && _constant[r] != constant) throw idb::verilog::detail::Error(loc, "conflicting constant drivers");
      _constant[r] = constant; return;
    }
    auto ra = root(a), rb = root(b); if (ra == rb) return;
    if (_rank[ra] < _rank[rb]) std::swap(ra, rb);
    _parent[rb] = ra; if (_rank[ra] == _rank[rb]) ++_rank[ra];
    if (_constant[ra] != idb::verilog::xBit && _constant[rb] != idb::verilog::xBit && _constant[ra] != _constant[rb])
      throw idb::verilog::detail::Error(loc, "conflicting constant drivers");
    if (_constant[ra] == idb::verilog::xBit) _constant[ra] = _constant[rb];
  }
 private:
  std::vector<NetId> _parent, _constant; std::vector<uint8_t> _rank;
};

std::vector<std::string> formalNames(const LibraryStore& library, LibraryCellMasterId master,
                                     const idb::verilog::LibraryCell& cell, const idb::verilog::PinConnection& port)
{
  const auto found = cell.ports.find(port.name);
  if (found == cell.ports.end() || !found->second.range || indexed(port.name)) return {port.name};
  const auto [left, right] = *found->second.range; const auto step = left <= right ? 1 : -1;
  std::vector<std::string> names;
  for (int32_t index = left;; index += step) {
    const auto name = port.name + "[" + std::to_string(index) + "]";
    if (library.masterTermStorage().findMasterTerm(master, name)) names.push_back(name);
    if (index == right) break;
  }
  return names;
}
}

VerilogNetlistImportError::VerilogNetlistImportError(std::size_t line, std::string message)
    : std::runtime_error(line == 0 ? message : "Verilog import error at line " + std::to_string(line) + ": " + message), _line(line) {}

VerilogNetlistImportResult VerilogNetlistImportService::import(const std::filesystem::path& file, std::string_view top_module)
{ return import(VerilogNetlistImportOptions{.file = file, .top_module = std::string(top_module)}); }

VerilogNetlistImportResult VerilogNetlistImportService::import(const VerilogNetlistImportOptions& options)
{
  if (options.file.empty()) throw VerilogNetlistImportError(0, "Verilog input path is empty");
  auto* library = _context.library(); if (library == nullptr) throw VerilogNetlistImportError(0, "Verilog import requires a loaded EccDB library");
  LibraryLookup lookup(*library);
  const auto result = idb::verilog::compileFiles({options.file.string()}, options.top_module, {}, lookup);
  if (!result) {
    if (!result.diagnostics.empty()) throw VerilogNetlistImportError(result.diagnostics.front().location.line, result.diagnostics.front().text());
    throw VerilogNetlistImportError(0, "failed to compile Verilog input");
  }
  const auto& flat = *result.design;
  auto& design = _context.design();
  if (!design.globalStorage().hasInfo()) design.globalStorage().setInfo(DesignInfo{.name = flat.top, .database_units_per_micron = 1000});
  Aliases aliases(flat.nets.size());
  for (NetId i = 0; i < flat.nets.size(); ++i) aliases.supply(i, flat.nets[i].type);
  std::size_t alias_count = 0;
  for (const auto& edge : flat.inouts) { aliases.join(edge.target, edge.source, edge.location); ++alias_count; }
  for (const auto& edge : flat.assignments) { aliases.join(edge.target, edge.source, edge.location); ++alias_count; }

  ConnectivityService connectivity(_context); InstanceService instance_service(_context); auto& netlist = design.netlistStorage();
  std::unordered_map<NetId, DesignNetId> net_ids;
  const auto netFor = [&](NetId value) -> DesignNetId {
    const auto key = aliases.canonical(value); if (key == idb::verilog::xBit || key == idb::verilog::zBit) throw VerilogNetlistImportError(0, "X/Z net cannot be connected");
    if (const auto found = net_ids.find(key); found != net_ids.end()) return found->second;
    DesignNet net;
    if (key == idb::verilog::zeroBit) net = DesignNet{.name = "__ecc_verilog_constant_0", .use = DesignSignalUse::kGround, .source = DesignNetSource::kNetlist};
    else if (key == idb::verilog::oneBit) net = DesignNet{.name = "__ecc_verilog_constant_1", .use = DesignSignalUse::kPower, .source = DesignNetSource::kNetlist};
    else net = DesignNet{.name = flat.nets.at(key).name, .use = toUse(flat.nets.at(key).type), .source = DesignNetSource::kNetlist};
    const auto id = connectivity.createNet(std::move(net)); net_ids.emplace(key, id); return id;
  };
  for (NetId i = 0; i < flat.nets.size(); ++i) static_cast<void>(netFor(i));

  std::size_t block_terms = 0;
  for (const auto& port : flat.ports) for (std::size_t bit = 0; bit < port.signal.bits.size(); ++bit) {
    std::string name = port.name;
    if (port.range) { const auto index = port.range->first + (port.range->first <= port.range->second ? static_cast<int32_t>(bit) : -static_cast<int32_t>(bit)); name += "[" + std::to_string(index) + "]"; }
    const auto term = connectivity.createBlockTerm(DesignBlockTerm{.name = name, .direction = toIoDir(port.direction), .use = DesignSignalUse::kSignal});
    connectivity.connect(term, netFor(port.signal.bits[bit])); ++block_terms;
  }

  std::size_t instance_terms = 0;
  for (const auto& cell : flat.instances) {
    const auto master = library->cellMasterStorage().findCellMaster(cell.type); if (!master) throw VerilogNetlistImportError(cell.location.line, "cell master '" + cell.type + "' was not found");
    const auto instance = instance_service.create(DesignInstance{.name = cell.name, .master = master, .source = DesignInstanceSource::kNetlist});
    const auto* interface = lookup(cell.type); if (interface == nullptr) throw VerilogNetlistImportError(cell.location.line, "no Verilog interface for '" + cell.type + "'");
    for (const auto& port : cell.ports) {
      if (port.signal.bits.empty()) continue;
      const auto names = formalNames(*library, master, *interface, port); if (names.size() != port.signal.bits.size()) throw VerilogNetlistImportError(port.location.line, "instance port width mismatch");
      for (std::size_t bit = 0; bit < names.size(); ++bit) {
        const auto term = netlist.findInstanceTerm(instance, names[bit]); if (!term) throw VerilogNetlistImportError(port.location.line, "materialized instance terminal missing: " + names[bit]);
        connectivity.connect(term, netFor(port.signal.bits[bit])); ++instance_terms;
      }
    }
  }
  return VerilogNetlistImportResult{.top_module = flat.top, .instances = flat.instances.size(), .instance_terms = instance_terms,
                                    .block_terms = block_terms, .nets = net_ids.size(), .aliases = alias_count};
}
}  // namespace eccdb
