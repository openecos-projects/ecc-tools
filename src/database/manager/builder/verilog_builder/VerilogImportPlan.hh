// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once
#include <memory>

#include "verilog/VerilogNetlist.hh"
namespace idb::verilog_import {
class VerilogLibrary;
struct PlannedNet
{
  std::string name;
  verilog::NetType type;
};
struct PlannedPin
{
  std::string name;
  verilog::NetId net;
  verilog::Direction direction;
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
// Fully owned, validated physical connectivity. No AST/LEF pointers or expressions.
struct VerilogImportPlan
{
  std::string top;
  std::vector<PlannedNet> nets;
  std::vector<PlannedPin> ports;
  std::vector<PlannedBus> port_buses;
  std::vector<PlannedBus> net_buses;
  std::vector<PlannedInstance> instances;
};
struct ImportPlanResult
{
  std::unique_ptr<VerilogImportPlan> plan;
  std::vector<verilog::Diagnostic> diagnostics;
  explicit operator bool() const { return plan != nullptr; }
};
ImportPlanResult makeImportPlan(const verilog::FlatDesign& flat, VerilogLibrary& library);
}  // namespace idb::verilog_import
