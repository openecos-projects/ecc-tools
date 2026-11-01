// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "VerilogNetlistImportService.h"

#include <charconv>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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
#include "verilog/VerilogImportPlan.hh"
#include "verilog/VerilogTypes.hh"

namespace eccdb {
namespace {
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
  const idb::verilog::ImportCellInterface* find(std::string_view name)
  {
    if (const auto cached = _cells.find(std::string(name)); cached != _cells.end()) return &cached->second;
    const auto master = _library.cellMasterStorage().findCellMaster(name);
    if (!master) return nullptr;
    idb::verilog::ImportCellInterface result;
    const auto& cell = _library.cellMasterStorage().cellMaster(master);
    std::map<std::string, std::map<int32_t, LibraryMasterTermId, std::greater<int32_t>>> buses;
    for (const auto term_id : cell.terms) {
      const auto& term = _library.masterTermStorage().masterTerm(term_id);
      result.ports.emplace(term.name, idb::verilog::ImportCellPort{{term.name}, toDir(term.direction), {}});
      if (const auto item = indexed(term.name); item && item->index >= 0) buses[item->base].emplace(item->index, term_id);
    }
    for (const auto& [base, members] : buses) {
      if (result.ports.contains(base)) continue;
      const auto high = members.begin()->first;
      const auto low = members.rbegin()->first;
      if (high > static_cast<int32_t>(idb::verilog::BitVector::kMaxWidth)
          || static_cast<int64_t>(high) - low + 1 != static_cast<int64_t>(members.size())) continue;
      const auto direction = toDir(_library.masterTermStorage().masterTerm(members.begin()->second).direction);
      idb::verilog::ImportCellPort port{{}, direction, std::pair<int, int>{high, low}};
      for (const auto& [index, term_id] : members) {
        const auto& term = _library.masterTermStorage().masterTerm(term_id);
        if (toDir(term.direction) != direction)
          throw idb::verilog::detail::Error({}, "inconsistent LEF bus directions: master=" + cell.name + ", port=" + base);
        port.pins.push_back(term.name);
      }
      result.has_buses = true;
      result.ports.emplace(base, std::move(port));
    }
    for (const auto& [name, port] : result.ports)
      result.semantic.ports.emplace(name, idb::verilog::PortShape{port.direction, static_cast<uint32_t>(port.pins.size()),
                                                                false, false, idb::verilog::NetType::wire, false, port.range});
    return &_cells.emplace(std::string(name), std::move(result)).first->second;
  }
  const idb::verilog::LibraryCell* lookup(std::string_view name)
  {
    const auto* interface = find(name);
    return interface ? &interface->semantic : nullptr;
  }
 private:
  const LibraryStore& _library;
  std::map<std::string, idb::verilog::ImportCellInterface> _cells;
};
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
  auto result = idb::verilog::compileFiles({options.file.string()}, options.top_module, {},
                                         [&](std::string_view name) { return lookup.lookup(name); });
  if (!result) {
    if (!result.diagnostics.empty()) throw VerilogNetlistImportError(result.diagnostics.front().location.line, result.diagnostics.front().text());
    throw VerilogNetlistImportError(0, "failed to compile Verilog input");
  }
  auto planned = idb::verilog::makeImportPlan(*result.design, [&](std::string_view name) { return lookup.find(name); });
  if (!planned) {
    if (!planned.diagnostics.empty())
      throw VerilogNetlistImportError(planned.diagnostics.front().location.line, planned.diagnostics.front().text());
    throw VerilogNetlistImportError(0, "failed to plan Verilog input");
  }
  result.design.reset();
  const auto& plan = *planned.plan;
  auto& design = _context.design();
  if (!design.globalStorage().hasInfo()) design.globalStorage().setInfo(DesignInfo{.name = plan.top, .database_units_per_micron = 1000});

  ConnectivityService connectivity(_context); InstanceService instance_service(_context); auto& netlist = design.netlistStorage();
  std::vector<DesignNetId> net_ids;
  net_ids.reserve(plan.nets.size());
  for (const auto& net : plan.nets) {
    const auto id = connectivity.createNet(DesignNet{.name = net.name, .use = toUse(net.type), .source = DesignNetSource::kNetlist});
    net_ids.push_back(id);
    if (!net.aliases.empty()) connectivity.setNetAliases(id, DesignNetAliases{net.aliases});
  }

  for (const auto& port : plan.ports) {
    const auto term = connectivity.createBlockTerm(DesignBlockTerm{.name = port.name, .direction = toIoDir(port.direction), .use = DesignSignalUse::kSignal});
    connectivity.connect(term, net_ids.at(port.net));
  }

  std::size_t instance_terms = 0;
  for (const auto& cell : plan.instances) {
    const auto master = library->cellMasterStorage().findCellMaster(cell.master);
    const auto instance = instance_service.create(DesignInstance{.name = cell.name, .master = master, .source = DesignInstanceSource::kNetlist});
    for (const auto& pin : cell.pins) {
      const auto term = netlist.findInstanceTerm(instance, pin.name);
      connectivity.connect(term, net_ids.at(pin.net));
      ++instance_terms;
    }
  }
  return VerilogNetlistImportResult{.top_module = plan.top, .instances = plan.instances.size(), .instance_terms = instance_terms,
                                    .block_terms = plan.ports.size(), .nets = net_ids.size(), .aliases = plan.alias_count};
}
}  // namespace eccdb
