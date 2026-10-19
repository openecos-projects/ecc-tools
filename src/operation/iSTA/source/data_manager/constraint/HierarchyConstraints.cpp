// SPDX-License-Identifier: MulanPSL-2.0
#include "HierarchyConstraints.hpp"

#include <limits>

#include "Database.hpp"

namespace ista {
namespace {
bool inside(Database& database, Pin& pin, const HierarchyPin& boundary)
{
  const auto found = database.get_hierarchy_instance_index().find(pin.get_instance_name());
  return !pin.get_is_port() && found != database.get_hierarchy_instance_index().end() && boundary.leaf_begin <= found->second
         && found->second < boundary.leaf_end;
}
bool driver(Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || pin.get_direction() == (pin.get_is_port() ? PinDirection::kInput : PinDirection::kOutput);
}
bool load(Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || pin.get_direction() == (pin.get_is_port() ? PinDirection::kOutput : PinDirection::kInput);
}
}  // namespace

const HierarchyPin& checkedHierarchyPin(Database& database, const std::string& name)
{
  const auto& pin = database.get_hierarchy_pin_map().at(name);
  if (!pin.error.empty())
    throw std::invalid_argument("hierarchy pin '" + pin.instance + "/" + pin.port + "': " + pin.error);
  return pin;
}

bool crossesHierarchyPin(Database& database, const HierarchyPin& boundary, std::string_view from, std::string_view to)
{
  auto d = database.get_pin_map().find(std::string(from));
  auto l = database.get_pin_map().find(std::string(to));
  if (d == database.get_pin_map().end() || l == database.get_pin_map().end() || from == to)
    return false;
  if (d->second.get_net_name() != boundary.net || l->second.get_net_name() != boundary.net || !driver(d->second) || !load(l->second))
    return false;
  const bool d_inside = inside(database, d->second, boundary), l_inside = inside(database, l->second, boundary);
  return (boundary.direction != PinDirection::kOutput && !d_inside && l_inside) || (boundary.direction != PinDirection::kInput && d_inside && !l_inside);
}

int hierarchyCrossingPosition(Database& database, const HierarchyPin& boundary, std::string_view from, std::string_view to)
{
  if (!crossesHierarchyPin(database, boundary, from, to))
    return std::numeric_limits<int>::min();
  return inside(database, database.get_pin_map().at(std::string(from)), boundary) ? -boundary.depth : boundary.depth;
}

std::vector<std::string> hierarchyDrivers(Database& database, const std::string& name)
{
  const auto& boundary = checkedHierarchyPin(database, name);
  std::vector<std::string> result;
  for (const auto& pin_name : database.get_net_map().at(boundary.net).get_pin_name_list()) {
    auto& pin = database.get_pin_map().at(pin_name);
    if (driver(pin)
        && ((boundary.direction != PinDirection::kOutput && !inside(database, pin, boundary))
            || (boundary.direction != PinDirection::kInput && inside(database, pin, boundary))))
      result.push_back(pin_name);
  }
  return result;
}

void setClockSources(Database& database, TimingClock& clock, const std::vector<std::string>& objects)
{
  std::set<std::string> roots;
  for (const auto& object : objects) {
    if (database.get_hierarchy_pin_map().contains(object)) {
      const auto drivers = hierarchyDrivers(database, object);
      if (drivers.empty())
        throw std::invalid_argument("hierarchical clock source has no leaf driver: " + object);
      roots.insert(drivers.begin(), drivers.end());
    } else
      roots.insert(object);
  }
  clock.set_source_objects(objects);
  clock.set_source_list({roots.begin(), roots.end()});
}

bool matchesClockSource(Database& database, const std::string& source, const std::string& pin)
{
  if (!database.get_hierarchy_pin_map().contains(source))
    return source == pin;
  const auto drivers = hierarchyDrivers(database, source);
  return std::find(drivers.begin(), drivers.end(), pin) != drivers.end();
}

bool clockAllowsNetArc(Database& database, const std::string& name, const std::string& from, const std::string& to, int before)
{
  if (database.get_hierarchy_pin_map().empty())
    return true;
  const auto& clock = database.get_timing_constraint().get_clock_map().at(name);
  const auto& objects = clock.get_source_objects();
  int current = std::numeric_limits<int>::min();
  if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), from) != clock.get_source_list().end()) {
    bool allowed = false;
    for (const auto& object : objects) {
      if (!database.get_hierarchy_pin_map().contains(object))
        allowed |= object == from;
      else {
        const int position = hierarchyCrossingPosition(database, checkedHierarchyPin(database, object), from, to);
        if (position > before)
          continue;
        current = std::max(current, position);
        allowed |= position != std::numeric_limits<int>::min();
      }
    }
    if (!allowed)
      return false;
  }
  // Only a boundary downstream of this clock's declared source replaces it.
  // Multiple clocks created with -add at the same boundary coexist.
  for (const auto& [other_name, other] : database.get_timing_constraint().get_clock_map()) {
    if (other_name == name)
      continue;
    for (const auto& object : other.get_source_objects()) {
      if (!database.get_hierarchy_pin_map().contains(object) || std::find(objects.begin(), objects.end(), object) != objects.end())
        continue;
      const int position = hierarchyCrossingPosition(database, checkedHierarchyPin(database, object), from, to);
      if (position > current && position < before)
        return false;
    }
  }
  return true;
}
}  // namespace ista
