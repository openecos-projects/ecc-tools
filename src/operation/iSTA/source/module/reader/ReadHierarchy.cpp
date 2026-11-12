// SPDX-License-Identifier: MulanPSL-2.0
#include "ReadHierarchy.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "VerilogImportPlan.hh"
#include "VerilogLibrary.hh"
#include "network/Network.hpp"
#include "verilog/VerilogFrontend.hh"

namespace ista {
void linkHierarchy(Network& network, const idb::verilog_import::VerilogImportPlan& plan)
{
  if (plan.top != network.name())
    throw std::invalid_argument("hierarchy Verilog top does not match the physical design: " + plan.top);
  std::vector<std::set<std::string>> expected(plan.nets.size());
  // Ignore power pins excluded by the STA adapter. Retain missing signal leaves
  // as mismatches; never silently map a stale hierarchy onto a different design.
  for (const auto& port : plan.ports)
    expected.at(port.net).insert(port.name);
  for (const auto& instance : plan.instances) {
    for (const auto& pin : instance.pins) {
      const auto type = plan.nets.at(pin.net).type;
      if (type == idb::verilog::NetType::supply0 || type == idb::verilog::NetType::supply1)
        continue;
      expected.at(pin.net).insert(instance.name + ":" + pin.name);
    }
  }
  std::map<idb::verilog::NetId, std::pair<std::string, std::string>> resolved;
  std::map<std::pair<std::string, idb::verilog::NetId>, size_t> aliases;
  for (const auto& port : plan.hierarchical_ports)
    ++aliases[{port.instance, port.net}];
  std::unordered_map<std::string, size_t> instance_index;
  for (size_t i = 0; i < plan.instances.size(); ++i)
    instance_index.emplace(plan.instances[i].name, i);
  auto resolveNet = [&](idb::verilog::NetId net_id) -> const std::pair<std::string, std::string>& {
    auto [it, inserted] = resolved.try_emplace(net_id);
    auto& [net_name, error] = it->second;
    if (inserted) {
      const auto& names = expected.at(net_id);
      for (const auto& name : names) {
        const auto pin = network.pins().find(name);
        if (pin == network.pins().end() || pin->second.get_net_name().empty()) {
          error = "missing connected leaf pin '" + name + "'";
          break;
        }
        if (net_name.empty())
          net_name = pin->second.get_net_name();
        else if (net_name != pin->second.get_net_name()) {
          error = "Verilog net spans different physical nets at '" + name + "'";
          break;
        }
      }
      if (error.empty() && !net_name.empty()) {
        const auto physical = network.nets().find(net_name);
        if (physical == network.nets().end())
          error = "physical net not found: " + net_name;
        else
          for (const auto& pin : physical->second.get_pin_name_list()) {
            if (!names.contains(pin)) {
              error = "physical net has an unmatched pin '" + pin + "'";
              break;
            }
          }
      }
      if (error.empty() && net_name.empty())
        error = "boundary has no connected physical signal pins";
    }
    return it->second;
  };
  std::map<std::string, HierarchyPin> pins;
  for (const auto& port : plan.hierarchical_ports) {
    const auto& [net_name, error] = resolveNet(port.net);
    const auto direction = port.direction == idb::verilog::Direction::input    ? PinDirection::kInput
                           : port.direction == idb::verilog::Direction::output ? PinDirection::kOutput
                           : port.direction == idb::verilog::Direction::inout  ? PinDirection::kInout
                                                                               : PinDirection::kNone;
    const auto key = port.instance + ":" + port.name;
    const std::string boundary_error = aliases.at({port.instance, port.net}) > 1
                                           ? "multiple ports of this instance share one flattened net; feedthrough/aliased boundary topology is not supported"
                                           : error;
    if (network.pins().contains(key)
        || !pins.emplace(key, HierarchyPin{port.instance, port.name, direction, net_name, boundary_error, port.leaf_begin, port.leaf_end, port.depth}).second)
      throw std::invalid_argument("hierarchy pin name collision: " + key);
  }
  std::map<std::string, NetSegment> segments;
  for (const auto& segment : plan.net_segments) {
    // Supplies are excluded from the timing graph and from signal-net queries.
    if (plan.nets.at(segment.net).type == idb::verilog::NetType::supply0 || plan.nets.at(segment.net).type == idb::verilog::NetType::supply1)
      continue;
    const auto& [net_name, error] = resolveNet(segment.net);
    if (!segments.emplace(segment.name, NetSegment{segment.name, segment.scope, net_name, segment.pins, error}).second)
      throw std::invalid_argument("hierarchy net name collision: " + segment.name);
  }
  network.setNetSegments(std::move(segments));
  network.hierarchyPins() = std::move(pins);
  network.hierarchyIndices() = std::move(instance_index);
}

void readHierarchy(Network& network, idb::IdbLayout& layout, const std::string& file)
{
  idb::verilog_import::VerilogLibrary library(layout);
  auto flat = idb::verilog::compileFiles({file}, network.name(), {}, [&](std::string_view cell) { return library.lookup(cell); });
  if (!flat)
    throw std::invalid_argument("hierarchy Verilog: " + flat.diagnostics.front().text());
  auto plan = idb::verilog_import::makeImportPlan(*flat.design, library);
  if (!plan)
    throw std::invalid_argument("hierarchy connectivity: " + plan.diagnostics.front().text());
  flat.design.reset();
  linkHierarchy(network, *plan.plan);
}
}  // namespace ista
