// SPDX-License-Identifier: MulanPSL-2.0
#include "ObjectQuery.hpp"

namespace ista::sdc {
namespace {

bool isDriver(Pin& pin)
{
  return pin.get_is_port() ? pin.get_direction() == PinDirection::kInput : pin.get_direction() == PinDirection::kOutput;
}

std::vector<std::string> seedPins(Database& database, const std::vector<std::string>& objects, bool forward)
{
  std::set<std::string> pins;
  for (const std::string& object : objects) {
    // Collections returned by get_* already contain canonical identities.
    if (database.get_pin_map().contains(object)) {
      pins.insert(object);
    } else if (database.get_net_map().contains(object)) {
      for (const std::string& name : database.get_net_map().at(object).get_pin_name_list()) {
        auto pin = database.get_pin_map().find(name);
        if (pin != database.get_pin_map().end() && isDriver(pin->second))
          pins.insert(name);
      }
    } else if (database.get_instance_map().contains(object)) {
      for (const std::string& name : database.get_instance_map().at(object).get_pin_name_list()) {
        auto pin = database.get_pin_map().find(name);
        if (pin != database.get_pin_map().end() && isDriver(pin->second) == forward)
          pins.insert(name);
      }
    } else {
      throw std::invalid_argument("fanin/fanout requires a pin, port, net or cell collection: " + object);
    }
  }
  return {pins.begin(), pins.end()};
}

bool isBoundary(Database& database, const std::string& name, bool forward)
{
  Pin& pin = database.get_pin_map().at(name);
  if (pin.get_is_port())
    return forward ? !isDriver(pin) : isDriver(pin);
  auto inst = database.get_instance_map().find(pin.get_instance_name());
  if (inst == database.get_instance_map().end() || !inst->second.get_is_sequential())
    return false;
  if (!forward)
    return isDriver(pin);
  for (TimingCheckArc& check : inst->second.get_check_arc_list()) {
    if (check.get_data_port() == name)
      return true;
  }
  return false;
}

std::vector<std::string> adjacentPins(Database& database, const std::string& name, bool forward)
{
  Pin& pin = database.get_pin_map().at(name);
  std::set<std::string> adjacent;
  // SDC is evaluated before GraphBuilder: driver/load and arc maps are not
  // initialized yet. Use physical connectivity plus actual Liberty arcs.
  if (isDriver(pin) == forward) {
    auto net = database.get_net_map().find(pin.get_net_name());
    if (net != database.get_net_map().end()) {
      for (const std::string& other : net->second.get_pin_name_list()) {
        auto candidate = database.get_pin_map().find(other);
        if (other != name && candidate != database.get_pin_map().end() && isDriver(candidate->second) != forward)
          adjacent.insert(other);
      }
    }
  }
  if (!pin.get_is_port()) {
    auto inst = database.get_instance_map().find(pin.get_instance_name());
    if (inst != database.get_instance_map().end()) {
      auto cell = database.get_timing_library().get_cell_map().find(inst->second.get_cell_name());
      if (cell != database.get_timing_library().get_cell_map().end()) {
        for (TimingCellArc& arc : cell->second.get_cell_arc_list()) {
          if (!arc.get_is_timing_graph_arc() || arc.get_is_disable_arc())
            continue;
          if ((forward ? arc.get_source_port() : arc.get_sink_port()) != pin.get_pin_name())
            continue;
          const std::string other = pin.get_instance_name() + ":" + (forward ? arc.get_sink_port() : arc.get_source_port());
          if (database.get_pin_map().contains(other))
            adjacent.insert(other);
        }
      }
    }
  }
  return {adjacent.begin(), adjacent.end()};
}

std::vector<std::string> findCone(Database& database, const std::vector<std::string>& objects, bool forward, bool terminal_only, bool only_cells)
{
  std::set<std::string> visited;
  std::set<std::string> result;
  std::vector<std::string> pending = seedPins(database, objects, forward);
  while (!pending.empty()) {
    std::string current = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(current).second)
      continue;
    const bool boundary = isBoundary(database, current, forward);
    if (!terminal_only || boundary) {
      Pin& pin = database.get_pin_map().at(current);
      if (!only_cells)
        result.insert(current);
      else if (!pin.get_is_port())
        result.insert(pin.get_instance_name());
    }
    if (boundary)
      continue;
    const auto adjacent = adjacentPins(database, current, forward);
    pending.insert(pending.end(), adjacent.begin(), adjacent.end());
  }
  return {result.begin(), result.end()};
}

}  // namespace

std::vector<std::string> findFanin(Database& database, const std::vector<std::string>& objects, bool startpoints_only, bool only_cells)
{
  return findCone(database, objects, false, startpoints_only, only_cells);
}

std::vector<std::string> findFanout(Database& database, const std::vector<std::string>& objects, bool endpoints_only, bool only_cells)
{
  return findCone(database, objects, true, endpoints_only, only_cells);
}

}  // namespace ista::sdc
