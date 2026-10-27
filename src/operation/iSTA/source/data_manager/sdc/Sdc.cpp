// SPDX-License-Identifier: MulanPSL-2.0
#include "sdc/Sdc.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "liberty/LibertyLibrary.hpp"
#include "liberty/LibertyPort.hpp"
#include "network/Network.hpp"
#include "network/Pin.hpp"

namespace ista {

void Sdc::clearConstraints()
{
  auto path = std::move(_sdc_file_path);
  const auto next_revision = _revision + 1;
  *this = Sdc{};
  _revision = next_revision;
  _sdc_file_path = std::move(path);
}
void Sdc::setClockSources(const Network& network, TimingClock& clock, const std::vector<std::string>& objects) const
{
  std::set<std::string> roots;
  for (const auto& object : objects) {
    if (network.isHierarchyPin(object)) {
      const auto drivers = network.hierarchyDrivers(object);
      if (drivers.empty())
        throw std::invalid_argument("hierarchical clock source has no leaf driver: " + object);
      roots.insert(drivers.begin(), drivers.end());
    } else
      roots.insert(object);
  }
  std::map<std::string, ClockSourceKind> previous_kinds;
  const auto& previous = clock.get_source_objects();
  const auto& kinds = clock.get_source_kinds();
  for (std::size_t index = 0; index < previous.size() && index < kinds.size(); ++index)
    previous_kinds.emplace(previous[index], kinds[index]);
  std::vector<ClockSourceKind> updated_kinds;
  for (const auto& object : objects) {
    if (previous_kinds.contains(object))
      updated_kinds.push_back(previous_kinds.at(object));
    else if (network.isHierarchyPin(object))
      updated_kinds.push_back(ClockSourceKind::kPin);
    else if (const auto pin = network.pins().find(object); pin != network.pins().end())
      updated_kinds.push_back(pin->second.get_is_port() ? ClockSourceKind::kPort : ClockSourceKind::kPin);
    else
      updated_kinds.push_back(network.hasNet(object) ? ClockSourceKind::kNet : ClockSourceKind::kPort);
  }
  clock.set_source_kinds(updated_kinds);
  if (clock.get_generated_clock_definition()) {
    auto definition = *clock.get_generated_clock_definition();
    definition.targets = objects;
    definition.target_kinds = std::move(updated_kinds);
    clock.set_generated_clock_definition(std::move(definition));
  }
  clock.set_source_objects(objects);
  clock.set_source_list({roots.begin(), roots.end()});
}

bool Sdc::matchesClockSource(const Network& network, const std::string& source, const std::string& pin) const
{
  if (!network.isHierarchyPin(source))
    return source == pin;
  const auto drivers = network.hierarchyDrivers(source);
  return std::find(drivers.begin(), drivers.end(), pin) != drivers.end();
}

bool Sdc::clockAllowsNetArc(const Network& network, const std::string& name, const std::string& from, const std::string& to, int before) const
{
  if (!network.hasHierarchyPins())
    return true;
  const auto& clock = _clock_map.at(name);
  const auto& objects = clock.get_source_objects();
  int current = std::numeric_limits<int>::min();
  if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), from) != clock.get_source_list().end()) {
    bool allowed = false;
    for (const auto& object : objects) {
      if (!network.isHierarchyPin(object))
        allowed |= object == from;
      else {
        const int position = network.hierarchyCrossingPosition(network.checkedHierarchyPin(object), from, to);
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
  for (const auto& [other_name, other] : _clock_map) {
    if (other_name == name)
      continue;
    for (const auto& object : other.get_source_objects()) {
      if (!network.isHierarchyPin(object) || std::find(objects.begin(), objects.end(), object) != objects.end())
        continue;
      const int position = network.hierarchyCrossingPosition(network.checkedHierarchyPin(object), from, to);
      if (position > current && position < before)
        return false;
    }
  }
  return true;
}
std::vector<std::pair<const LibertyCell*, const LibertyPort*>> Sdc::drivingCellPorts(const LibertyLibrary& liberty, const std::string& pin) const
{
  std::vector<std::pair<const LibertyCell*, const LibertyPort*>> result;
  const auto constraint = _port_constraint_map.find(pin);
  if (constraint == _port_constraint_map.end())
    return result;
  for (const auto& [analysis, edges] : constraint->second.drivingCells())
    for (const auto& [edge, drive] : edges) {
      if (drive.get_no_design_rule())
        continue;
      const auto cell = liberty.get_cell_map().find(drive.get_cell_name());
      if (cell == liberty.get_cell_map().end())
        continue;
      const auto port = cell->second.get_port_map().find(drive.get_to_pin());
      if (port != cell->second.get_port_map().end())
        result.emplace_back(&cell->second, &port->second);
    }
  return result;
}

std::optional<double> Sdc::fanoutLimit(const Network& network, const LibertyLibrary& liberty, const std::string& driver) const
{
  const Pin& pin = network.pins().at(driver);
  std::optional<double> pin_limit;
  if (pin.get_is_port()) {
    const auto port = _port_max_fanout_map.find(driver);
    if (port != _port_max_fanout_map.end())
      pin_limit = port->second;
    for (const auto& [cell, output] : drivingCellPorts(liberty, driver))
      if (auto inherited = output->get_max_fanout(); inherited && (!pin_limit || *inherited < *pin_limit))
        pin_limit = inherited;
  } else if (const auto* port = network.libertyPort(liberty, pin)) {
    pin_limit = port->get_max_fanout();
  }
  if (!pin_limit)
    return _max_fanout;
  return _max_fanout ? std::min(*_max_fanout, *pin_limit) : *pin_limit;
}

namespace {
std::set<TransType> uncertaintyEdges(TransType edge)
{
  return edge == TransType::kNone ? std::set{TransType::kRise, TransType::kFall} : std::set{edge};
}
TransType uncertaintyEdge(const std::set<TransType>& edges)
{
  return edges.size() == 2 ? TransType::kNone : *edges.begin();
}
std::vector<TimingClockUncertainty> subtractUncertainty(const TimingClockUncertainty& existing, const TimingClockUncertainty& filter)
{
  // Subtract a rectangle one axis at a time. At most five remainders per
  // record are needed; clock sets are never expanded into a Cartesian product.
  TimingClockUncertainty overlap = existing;
  std::vector<TimingClockUncertainty> result;
  const auto split = [&](const auto& selected, auto getter, auto setter) {
    const auto values = getter(overlap);
    auto common = values;
    std::erase_if(common, [&](const auto& value) { return !selected.contains(value); });
    if (common.empty())
      return false;
    auto remainder = values;
    std::erase_if(remainder, [&](const auto& value) { return common.contains(value); });
    if (!remainder.empty()) {
      auto piece = overlap;
      setter(piece, remainder);
      result.push_back(std::move(piece));
    }
    setter(overlap, common);
    return true;
  };
  const auto checks = [](const TimingClockUncertainty& u) {
    std::set<AnalysisType> values;
    if (u.get_setup())
      values.insert(AnalysisType::kMax);
    if (u.get_hold())
      values.insert(AnalysisType::kMin);
    return values;
  };
  if (!split(
          filter.get_from_clocks(), [](const auto& u) { return u.get_from_clocks(); }, [](auto& u, const auto& v) { u.set_from_clocks(v); })
      || !split(
          filter.get_to_clocks(), [](const auto& u) { return u.get_to_clocks(); }, [](auto& u, const auto& v) { u.set_to_clocks(v); })
      || !split(
          uncertaintyEdges(filter.get_from_trans_type()), [](const auto& u) { return uncertaintyEdges(u.get_from_trans_type()); },
          [](auto& u, const auto& v) { u.set_from_trans_type(uncertaintyEdge(v)); })
      || !split(
          uncertaintyEdges(filter.get_to_trans_type()), [](const auto& u) { return uncertaintyEdges(u.get_to_trans_type()); },
          [](auto& u, const auto& v) { u.set_to_trans_type(uncertaintyEdge(v)); })
      || !split(checks(filter), checks, [](auto& u, const auto& v) {
           u.set_setup(v.contains(AnalysisType::kMax));
           u.set_hold(v.contains(AnalysisType::kMin));
         }))
    return {existing};
  return result;
}
bool separates(const TimingClockGroup& relation, const std::string& first, const std::string& second)
{
  const auto& groups = relation.get_groups();
  if (groups.size() == 1)
    return groups.front().contains(first) != groups.front().contains(second);
  for (size_t i = 0; i < groups.size(); ++i)
    for (size_t j = i + 1; j < groups.size(); ++j)
      if ((groups[i].contains(first) && groups[j].contains(second)) || (groups[i].contains(second) && groups[j].contains(first)))
        return true;
  return false;
}
}  // namespace

void Sdc::removeClockUncertainty(const TimingClockUncertainty& filter)
{
  std::vector<TimingClockUncertainty> updated;
  for (const auto& existing : _clock_uncertainty_list) {
    auto remaining = subtractUncertainty(existing, filter);
    updated.insert(updated.end(), std::make_move_iterator(remaining.begin()), std::make_move_iterator(remaining.end()));
  }
  _clock_uncertainty_list.swap(updated);
}
void Sdc::setClockUncertainty(const TimingClockUncertainty& uncertainty)
{
  Sdc updated;
  updated._clock_uncertainty_list = _clock_uncertainty_list;
  updated.removeClockUncertainty(uncertainty);
  updated._clock_uncertainty_list.push_back(uncertainty);
  _clock_uncertainty_list.swap(updated._clock_uncertainty_list);
}
void Sdc::setClockGroups(TimingClockGroup relation)
{
  for (const auto& existing : _clock_group_list) {
    if (!relation.get_name().empty() && existing.get_name() == relation.get_name())
      continue;  // A named group replaces its previous definition.
    if (existing.get_type() != TimingClockGroupType::kAsynchronous || relation.get_type() != TimingClockGroupType::kAsynchronous
        || existing.get_allow_paths() == relation.get_allow_paths())
      continue;
    for (auto first = _clock_map.begin(); first != _clock_map.end(); ++first)
      for (auto second = std::next(first); second != _clock_map.end(); ++second)
        if (separates(existing, first->first, second->first) && separates(relation, first->first, second->first))
          throw std::invalid_argument("conflicting asynchronous clock groups with and without -allow_paths");
  }
  auto updated = _clock_group_list;
  if (!relation.get_name().empty())
    std::erase_if(updated, [&](const auto& old) { return old.get_name() == relation.get_name(); });
  updated.push_back(std::move(relation));
  _clock_group_list.swap(updated);
}
}  // namespace ista
