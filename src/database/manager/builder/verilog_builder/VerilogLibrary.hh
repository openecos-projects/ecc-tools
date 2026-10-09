// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once
#include <unordered_map>
#include <vector>

#include "verilog/VerilogTypes.hh"
namespace idb {
class IdbLayout;
class IdbCellMaster;
namespace verilog_import {
struct CellPort
{
  std::vector<std::string> pins;  // LEF indexed pin groups are connected most significant index first.
  verilog::Direction direction;
  std::optional<std::pair<int, int>> range;
};
struct CellInterface
{
  std::unordered_map<std::string, CellPort> ports;
  bool has_buses = false;
  verilog::LibraryCell semantic;
};

// Owns interface snapshots; it never exposes borrowed LEF terms to a plan.
class VerilogLibrary
{
 public:
  explicit VerilogLibrary(IdbLayout& layout) : _layout(layout) {}
  const CellInterface* find(std::string_view cell);
  const verilog::LibraryCell* lookup(std::string_view cell);
  static CellInterface describe(IdbCellMaster& master);

 private:
  IdbLayout& _layout;
  std::unordered_map<std::string, CellInterface> _interfaces;
};
}  // namespace verilog_import
}  // namespace idb
