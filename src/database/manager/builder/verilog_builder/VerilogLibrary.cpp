// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogLibrary.hh"

#include <charconv>
#include <map>

#include "IdbLayout.h"
#include "verilog/VerilogValue.hh"
namespace idb::verilog_import {
using namespace verilog;
namespace {
Direction direction(IdbConnectDirection value)
{
  switch (value) {
    case IdbConnectDirection::kInput:
      return Direction::input;
    case IdbConnectDirection::kOutput:
      return Direction::output;
    case IdbConnectDirection::kInOut:
      return Direction::inout;
    default:
      return Direction::none;
  }
}
std::optional<std::pair<std::string, int>> pinIndex(const std::string& name)
{
  if (name.empty() || name.back() != ']')
    return std::nullopt;
  const auto bracket = name.rfind('[');
  if (bracket == std::string::npos)
    return std::nullopt;
  int index;
  const auto result = std::from_chars(name.data() + bracket + 1, name.data() + name.size() - 1, index);
  if (result.ec != std::errc{} || result.ptr != name.data() + name.size() - 1 || index < 0)
    return std::nullopt;
  return std::pair<std::string, int>{name.substr(0, bracket), index};
}
}  // namespace
CellInterface VerilogLibrary::describe(IdbCellMaster& master)
{
  CellInterface result;
  std::map<std::string, std::map<int, IdbTerm*, std::greater<int>>> groups;
  for (auto* term : master.get_term_list()) {
    const auto name = term->get_name();
    result.ports.emplace(name, CellPort{{name}, direction(term->get_direction()), {}});
    if (auto index = pinIndex(name))
      groups[index->first].emplace(index->second, term);
  }
  for (const auto& [name, terms] : groups) {
    if (result.ports.count(name))
      continue;  // An exact scalar LEF pin takes precedence over a bus group.
    const int high = terms.begin()->first, low = terms.rbegin()->first;
    if (high > int(BitVector::kMaxWidth) || int64_t(high) - low + 1 != int64_t(terms.size()))
      continue;
    CellPort port{{}, direction(terms.begin()->second->get_direction()), std::pair<int, int>{high, low}};
    for (const auto& [index, term] : terms) {
      if (direction(term->get_direction()) != port.direction)
        throw verilog::detail::Error({}, "inconsistent LEF bus directions: master=" + master.get_name() + ", port=" + name);
      port.pins.push_back(term->get_name());
    }
    result.has_buses = true;
    result.ports.emplace(name, std::move(port));
  }
  return result;
}

const CellInterface* VerilogLibrary::find(std::string_view cell)
{
  auto found = _interfaces.find(std::string(cell));
  if (found != _interfaces.end())
    return &found->second;
  auto* master = _layout.get_cell_master_list()->find_cell_master(std::string(cell));
  if (!master)
    return nullptr;
  return &_interfaces.emplace(cell, describe(*master)).first->second;
}
std::optional<PortShape> VerilogLibrary::resolve(std::string_view cell, std::string_view port)
{
  const auto* interface = find(cell);
  if (!interface)
    return std::nullopt;
  const auto found = interface->ports.find(std::string(port));
  if (found == interface->ports.end())
    return std::nullopt;
  return PortShape{found->second.direction, static_cast<uint32_t>(found->second.pins.size())};
}
}  // namespace idb::verilog_import
