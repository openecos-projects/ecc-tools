// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "VerilogNetlist.hh"

namespace idb::verilog {

// Private import/export convention: physical databases have power/ground nets,
// but no four-state value field.
inline constexpr std::string_view verilogZeroNet = "__ecc_verilog_constant_0";
inline constexpr std::string_view verilogOneNet = "__ecc_verilog_constant_1";

struct ImportCellPort
{
  std::vector<std::string> pins;  // LEF indexed pin groups are connected most significant index first.
  Direction direction;
  std::optional<std::pair<int, int>> range;
};

struct ImportCellInterface
{
  std::unordered_map<std::string, ImportCellPort> ports;
  bool has_buses = false;
  LibraryCell semantic;
};

using ImportLibraryLookup = std::function<const ImportCellInterface*(std::string_view)>;

struct PlannedNet
{
  std::string name;
  NetType type;
  // Singleton ordinary nets are represented by name alone. Alias groups retain
  // every flattened symbol, including the canonical name when present.
  std::vector<std::string> aliases;
};

struct PlannedPin
{
  std::string name;
  NetId net;
  Direction direction;
};

struct BusMember
{
  unsigned index;
  uint32_t member;
};

// member indexes the enclosing net/pin array. Indices come from declarations,
// never from names chosen by alias merging.
struct PlannedBus
{
  std::string name;
  unsigned left;
  unsigned right;
  std::vector<BusMember> members;
};

struct PlannedInstance
{
  std::string name;
  std::string master;
  std::vector<PlannedPin> pins;
  std::vector<PlannedBus> buses;
};

struct PlannedNetSegment
{
  std::string name;
  std::string scope;
  NetId net;
  std::vector<std::string> pins;
};

// Fully owned, validated physical connectivity. No AST, LEF, or expression
// objects are borrowed by the plan.
struct VerilogImportPlan
{
  std::string top;
  std::vector<PlannedNet> nets;
  std::vector<PlannedPin> ports;
  std::vector<PlannedBus> port_buses;
  std::vector<PlannedBus> net_buses;
  std::vector<PlannedInstance> instances;
  std::vector<HierarchicalPort> hierarchical_ports;
  std::vector<PlannedNetSegment> net_segments;
  std::size_t alias_count = 0;
};

struct ImportPlanResult
{
  std::unique_ptr<VerilogImportPlan> plan;
  std::vector<Diagnostic> diagnostics;
  explicit operator bool() const { return plan != nullptr; }
};

ImportPlanResult makeImportPlan(const Netlist& flat, const ImportLibraryLookup& lookup);

}  // namespace idb::verilog
