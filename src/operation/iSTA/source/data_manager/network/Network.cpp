// SPDX-License-Identifier: MulanPSL-2.0
#include "network/Network.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "liberty/LibertyLibrary.hpp"

namespace ista {
namespace {
bool inside(const Network& network, const Pin& pin, const HierarchyPin& boundary)
{
  const auto found = network.hierarchyIndices().find(pin.get_instance_name());
  return !pin.get_is_port() && found != network.hierarchyIndices().end() && boundary.leaf_begin <= found->second && found->second < boundary.leaf_end;
}
bool driver(const Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || pin.get_direction() == (pin.get_is_port() ? PinDirection::kInput : PinDirection::kOutput);
}
bool load(const Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || pin.get_direction() == (pin.get_is_port() ? PinDirection::kOutput : PinDirection::kInput);
}
}  // namespace

void Network::setNetSegments(std::map<std::string, NetSegment> segments)
{
  decltype(_pin_nets) pin_nets;
  for (const auto& [name, segment] : segments)
    for (const auto& pin : segment.pins)
      pin_nets[pin].push_back(name);
  decltype(_net_query_names) query_names;
  for (const auto& [name, segment] : segments) {
    if (!segment.error.empty())
      continue;
    auto [it, inserted] = query_names.try_emplace(segment.physical_net, name);
    if (!inserted && segment.scope.size() < segments.at(it->second).scope.size())
      it->second = name;
  }
  _net_query_names = std::move(query_names);
  _net_segments = std::move(segments);
  _pin_nets = std::move(pin_nets);
}

const NetSegment& Network::netSegment(const std::string& name) const
{
  const auto& segment = _net_segments.at(name);
  if (!segment.error.empty())
    throw std::invalid_argument("net segment '" + name + "': " + segment.error);
  return segment;
}

bool Network::hasNet(const std::string& name) const
{
  return _net_segments.empty() ? _nets.contains(name) : _net_segments.contains(name);
}

const std::string& Network::physicalNet(const std::string& name) const
{
  if (!_net_segments.empty())
    return netSegment(name).physical_net;
  const auto found = _nets.find(name);
  if (found == _nets.end())
    throw std::invalid_argument("net no longer exists: " + name);
  return found->first;
}

std::string Network::netQueryName(const std::string& physical_name) const
{
  if (_net_segments.empty())
    return physical_name;
  const auto found = _net_query_names.find(physical_name);
  if (found == _net_query_names.end())
    throw std::invalid_argument("physical net has no logical declaration: " + physical_name);
  return found->second;
}

std::vector<std::string> Network::netSegmentPins(const std::string& name, bool leaf) const
{
  if (!leaf && !_net_segments.empty()) {
    const auto& segment = netSegment(name);
    for (const auto& pin : segment.pins)
      if (isHierarchyPin(pin))
        checkedHierarchyPin(pin);
    return segment.pins;
  }
  return _nets.at(physicalNet(name)).get_pin_name_list();
}

const std::vector<std::string>& Network::pinNets(const std::string& pin) const
{
  static const std::vector<std::string> empty;
  const auto found = _pin_nets.find(pin);
  return found == _pin_nets.end() ? empty : found->second;
}

std::set<std::string> Network::hierarchyCells() const
{
  std::set<std::string> cells;
  for (const auto& [name, pin] : hierarchyPins()) {
    auto instance = pin.instance;
    int depth = pin.depth;
    while (!instance.empty()) {
      cells.insert(instance);
      if (depth == 1)
        break;  // A slash in an escaped root name is not an ancestor.
      const auto slash = instance.rfind('/');
      if (slash == std::string::npos)
        break;
      instance.resize(slash);
      if (depth > 1)
        --depth;
    }
  }
  return cells;
}

const HierarchyPin& Network::checkedHierarchyPin(const std::string& name) const
{
  const auto& pin = hierarchyPins().at(name);
  if (!pin.error.empty())
    throw std::invalid_argument("hierarchy pin '" + pin.instance + "/" + pin.port + "': " + pin.error);
  return pin;
}

bool Network::crossesHierarchyPin(const HierarchyPin& boundary, std::string_view from, std::string_view to) const
{
  auto d = pins().find(std::string(from));
  auto l = pins().find(std::string(to));
  if (d == pins().end() || l == pins().end() || from == to)
    return false;
  if (d->second.get_net_name() != boundary.net || l->second.get_net_name() != boundary.net || !driver(d->second) || !load(l->second))
    return false;
  const bool d_inside = inside(*this, d->second, boundary), l_inside = inside(*this, l->second, boundary);
  return (boundary.direction != PinDirection::kOutput && !d_inside && l_inside) || (boundary.direction != PinDirection::kInput && d_inside && !l_inside);
}

int Network::hierarchyCrossingPosition(const HierarchyPin& boundary, std::string_view from, std::string_view to) const
{
  if (!crossesHierarchyPin(boundary, from, to))
    return std::numeric_limits<int>::min();
  return inside(*this, pins().at(std::string(from)), boundary) ? -boundary.depth : boundary.depth;
}

std::vector<std::string> Network::hierarchyDrivers(const std::string& name) const
{
  const auto& boundary = checkedHierarchyPin(name);
  std::vector<std::string> result;
  for (const auto& pin_name : nets().at(boundary.net).get_pin_name_list()) {
    auto& pin = pins().at(pin_name);
    if (driver(pin)
        && ((boundary.direction != PinDirection::kOutput && !inside(*this, pin, boundary))
            || (boundary.direction != PinDirection::kInput && inside(*this, pin, boundary))))
      result.push_back(pin_name);
  }
  return result;
}

bool Network::hasHierarchyPins() const
{
  return !hierarchyPins().empty();
}
bool Network::isHierarchyPin(const std::string& name) const
{
  return hierarchyPins().contains(name);
}

const LibertyPort* Network::libertyPort(const LibertyLibrary& liberty, const Pin& pin) const
{
  const auto instance = instances().find(pin.get_instance_name());
  if (instance == instances().end())
    return nullptr;
  const auto& cells = liberty.get_cell_map();
  const auto cell = cells.find(instance->second.get_cell_name());
  if (cell == cells.end())
    return nullptr;
  const auto port = cell->second.get_port_map().find(pin.get_pin_name());
  return port == cell->second.get_port_map().end() ? nullptr : &port->second;
}

TransType Network::launchClockTransition(std::string_view start) const
{
  const auto pin = pins().find(std::string(start));
  if (pin != pins().end() && !pin->second.get_is_port()) {
    const auto instance = instances().find(pin->second.get_instance_name());
    if (instance != instances().end() && instance->second.get_is_sequential()) {
      const std::vector<TimingArc>& arcs = instance->second.get_clock_to_q_arc().get_timing_arc_list();
      if (!arcs.empty() && arcs.front().get_trigger_trans_type() == TransType::kFall) {
        return TransType::kFall;
      }
    }
  }
  return TransType::kRise;
}

}  // namespace ista
