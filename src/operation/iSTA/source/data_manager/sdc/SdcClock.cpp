// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "network/Network.hpp"
#include "sdc/Sdc.hpp"

namespace ista {
namespace {
// Constraint installation precedes graph construction. Check structural reachability
// from retained master roots without borrowing stale propagated clock states.
bool masterReachesSource(const Network& network, const TimingClock& master, const std::string& source, const std::set<std::string>& displaced_roots)
{
  const auto targets = network.isHierarchyPin(source) ? network.hierarchyDrivers(source) : std::vector<std::string>{source};
  auto pending = master.get_source_list();
  std::set<std::string> visited;
  for (size_t i = 0; i < pending.size(); ++i) {
    const auto name = pending[i];
    if (displaced_roots.contains(name) || !visited.insert(name).second)
      continue;
    if (std::find(targets.begin(), targets.end(), name) != targets.end())
      return true;
    const auto found = network.pins().find(name);
    if (found == network.pins().end())
      continue;
    const auto& pin = found->second;
    const auto direction = pin.get_direction();
    const bool drives = direction == PinDirection::kInout || direction == (pin.get_is_port() ? PinDirection::kInput : PinDirection::kOutput);
    if (drives) {
      const auto net = network.nets().find(pin.get_net_name());
      if (net != network.nets().end())
        for (const auto& load_name : net->second.get_pin_name_list()) {
          const auto load = network.pins().find(load_name);
          if (load == network.pins().end() || load->second.get_net_name() != pin.get_net_name())
            continue;
          const auto& value = load->second;
          if (value.get_direction() == PinDirection::kInout || value.get_direction() == (value.get_is_port() ? PinDirection::kOutput : PinDirection::kInput))
            pending.push_back(load_name);
        }
    }
    if (!pin.get_is_port() && direction != PinDirection::kOutput) {
      const auto cell = network.instances().find(pin.get_instance_name());
      if (cell == network.instances().end() || (cell->second.get_is_sequential() && cell->second.get_clock_pin_name() != name))
        continue;
      for (const auto& output_name : cell->second.get_pin_name_list()) {
        const auto output = network.pins().find(output_name);
        if (output != network.pins().end() && output->second.get_direction() != PinDirection::kInput)
          pending.push_back(output_name);
      }
    }
  }
  return false;
}

TimingClock evaluateGeneratedClock(const std::map<std::string, TimingClock>& clocks, const std::string& name, GeneratedClockDefinition definition,
                                   const std::string& comment)
{
  std::string master_name;
  if (definition.master_clock.has_value()) {
    master_name = *definition.master_clock;
  } else {
    for (auto& [candidate, clock] : clocks) {
      if (std::find(clock.get_source_objects().begin(), clock.get_source_objects().end(), definition.master_source) != clock.get_source_objects().end()) {
        if (!master_name.empty()) {
          throw std::invalid_argument("ambiguous master; specify -master_clock");
        }
        master_name = candidate;
      }
    }
    if (master_name.empty() && clocks.size() == 1) {
      master_name = clocks.begin()->first;
    }
  }
  if (name.empty() || !clocks.contains(master_name)) {
    throw std::invalid_argument("generated clock master does not exist");
  }
  std::set<std::string> ancestors{name};
  for (std::string ancestor = master_name; !ancestor.empty();) {
    if (!ancestors.insert(ancestor).second) {
      throw std::invalid_argument("generated clock dependency cycle");
    }
    if (!clocks.contains(ancestor))
      throw std::invalid_argument("generated clock master does not exist: " + ancestor);
    ancestor = clocks.at(ancestor).get_master_clock_name();
  }
  const TimingClock& master = clocks.at(master_name);
  double master_period = master.get_period();
  std::vector<double> master_edges;
  for (size_t index = 0; index < master.edgeCount(); ++index)
    master_edges.push_back(master.edge(index).time);
  const auto invertWaveform = [](std::vector<double>& waveform, double period) {
    const double first = waveform.front();
    std::rotate(waveform.begin(), waveform.begin() + 1, waveform.end());
    waveform.back() = first + period;
  };
  if (definition.preinvert)
    invertWaveform(master_edges, master_period);
  if (definition.invert && definition.preinvert) {
    throw std::invalid_argument("-invert and -preinvert are mutually exclusive");
  }
  const int transformations = definition.divide_by.has_value() + definition.multiply_by.has_value() + definition.edges.has_value();
  if (transformations > 1) {
    throw std::invalid_argument("generated clock transformations are mutually exclusive");
  }
  const bool combinational = definition.combinational;
  if (transformations == 0 && !combinational) {
    throw std::invalid_argument("create_generated_clock requires -divide_by, -multiply_by, -edges, or -combinational");
  }
  if (combinational && definition.divide_by.has_value() && *definition.divide_by != 1.0) {
    throw std::invalid_argument("create_generated_clock -combinational only accepts -divide_by 1");
  }
  if (definition.invert && definition.edges.has_value()) {
    throw std::invalid_argument("create_generated_clock -invert cannot be used with -edges");
  }
  double period = master_period;
  std::vector<double> waveform = master_edges;
  for (const auto& [option, factor_value] : {std::pair{"-divide_by", definition.divide_by}, std::pair{"-multiply_by", definition.multiply_by}}) {
    if (!factor_value)
      continue;
    const double factor = *factor_value;
    if (!std::isfinite(factor) || factor < 1 || factor != std::floor(factor))
      throw std::invalid_argument("generated clock factor must be a positive integer");
    if (std::string_view(option) == "-multiply_by") {
      period /= factor;
      for (double& value : waveform)
        value /= factor;
    } else {
      period *= factor;
      const bool power_of_two = std::floor(std::log2(factor)) == std::log2(factor);
      if (factor > 1 && power_of_two) {
        for (size_t i = 0; i < waveform.size(); i += 2) {
          const double next = i + 2 < waveform.size() ? master_edges[i + 2] : master_edges.front() + master_period;
          waveform[i] = master_edges.front() + (master_edges[i] - master_edges.front()) * factor;
          waveform[i + 1] = waveform[i] + (next - master_edges[i]) * factor / 2;
        }
      } else
        for (double& value : waveform)
          value *= factor;
    }
  }
  if (definition.edges.has_value()) {
    const std::vector<double> edges = *definition.edges;
    const std::vector<double> shifts = definition.edge_shifts.has_value() ? *definition.edge_shifts : std::vector<double>(edges.size(), 0.0);
    if (edges.size() < 3 || edges.size() % 2 == 0 || shifts.size() != edges.size()) {
      throw std::invalid_argument("-edges requires an odd number of at least three values and matching -edge_shift");
    }
    std::vector<double> times;
    for (std::size_t i = 0; i < edges.size(); ++i) {
      if (!std::isfinite(edges[i]) || edges[i] < 1 || edges[i] != std::floor(edges[i]) || (i && edges[i] < edges[i - 1])) {
        throw std::invalid_argument("-edges requires nondecreasing positive integers");
      }
      const double index = edges[i] - 1;
      times.push_back(std::floor(index / master_edges.size()) * master_period + master_edges[static_cast<size_t>(std::fmod(index, master_edges.size()))]
                      + shifts[i]);
    }
    period = times.back() - times.front();
    waveform.assign(times.begin(), times.end() - 1);
  } else if (definition.edge_shifts.has_value()) {
    throw std::invalid_argument("-edge_shift requires -edges");
  }
  if (definition.duty_cycle.has_value()) {
    const double duty = *definition.duty_cycle;
    if (!definition.multiply_by.has_value() || !std::isfinite(duty) || duty <= 0 || duty >= 100) {
      throw std::invalid_argument("-duty_cycle requires -multiply_by and a percentage between 0 and 100");
    }
    for (size_t i = 0; i < waveform.size(); i += 2) {
      const double next = i + 2 < waveform.size() ? waveform[i + 2] : waveform.front() + period;
      waveform[i + 1] = waveform[i] + (next - waveform[i]) * duty / 100;
    }
  }
  if (definition.invert)
    invertWaveform(waveform, period);
  if (!std::isfinite(period) || period <= 0 || waveform.back() >= waveform.front() + period)
    throw std::invalid_argument("invalid generated clock waveform");
  for (size_t i = 0; i < waveform.size(); ++i)
    if (!std::isfinite(waveform[i]) || (i && waveform[i] <= waveform[i - 1]))
      throw std::invalid_argument("invalid generated clock waveform");
  TimingClock generated;
  generated.set_clock_name(name);
  generated.set_master_clock_name(master_name);
  generated.set_master_source(definition.master_source);
  definition.master_clock = master_name;
  generated.set_source_objects(definition.targets);
  generated.set_source_kinds(definition.target_kinds);
  generated.set_generated_clock_definition(std::move(definition));
  generated.set_period(period);
  generated.set_waveform(std::move(waveform));
  generated.set_comment(comment);
  return generated;
}
}  // namespace

TimingClock Sdc::makeGeneratedClock(const std::string& name, GeneratedClockDefinition definition, const std::string& comment) const
{
  return evaluateGeneratedClock(_clock_map, name, std::move(definition), comment);
}

void Sdc::defineClock(const Network& network, TimingClock clock, const std::vector<std::string>& sources, bool add)
{
  const std::string name = clock.get_clock_name();
  setClockSources(network, clock, sources);
  auto updated = _clock_map;
  if (const auto previous = updated.find(name); previous != updated.end())
    clock.copyAnnotations(previous->second);
  if (!add && !sources.empty()) {
    for (auto iter = updated.begin(); iter != updated.end();) {
      auto& [other_name, other] = *iter;
      if (other_name == name || other.get_source_objects().empty()) {
        ++iter;
        continue;
      }
      auto remaining = other.get_source_objects();
      std::erase_if(remaining, [&](const auto& source) { return std::find(sources.begin(), sources.end(), source) != sources.end(); });
      if (remaining.empty())
        iter = updated.erase(iter);
      else {
        setClockSources(network, other, remaining);
        ++iter;
      }
    }
  }
  updated[name] = std::move(clock);
  std::map<std::string, std::vector<std::string>> children;
  std::vector<std::string> ready;
  for (const auto& [id, value] : updated) {
    if (!value.get_is_generated()) {
      ready.push_back(id);
      continue;
    }
    if (!updated.contains(value.get_master_clock_name()))
      throw std::invalid_argument("clock replacement would orphan generated clock: " + id);
    const auto old_master = _clock_map.find(value.get_master_clock_name());
    const auto& master = updated.at(value.get_master_clock_name());
    if (old_master != _clock_map.end()) {
      const auto& old_sources = old_master->second.get_source_objects();
      const auto& sources = master.get_source_objects();
      if (std::find(old_sources.begin(), old_sources.end(), value.get_master_source()) != old_sources.end()
          && std::find(sources.begin(), sources.end(), value.get_master_source()) == sources.end())
        throw std::invalid_argument("clock replacement would remove generated clock master source: " + id);
      if (old_sources != sources) {
        std::set<std::string> displaced;
        for (const auto& root : old_master->second.get_source_list())
          if (std::find(master.get_source_list().begin(), master.get_source_list().end(), root) == master.get_source_list().end())
            displaced.insert(root);
        if (!masterReachesSource(network, master, value.get_master_source(), displaced))
          throw std::invalid_argument("clock replacement disconnects generated clock master source: " + id);
      }
    }
    children[value.get_master_clock_name()].push_back(id);
  }
  for (size_t index = 0; index < ready.size(); ++index) {
    for (const auto& child : children[ready[index]]) {
      auto& previous = updated.at(child);
      if (!previous.get_generated_clock_definition())
        throw std::invalid_argument("missing generated clock definition: " + child);
      auto refreshed = evaluateGeneratedClock(updated, child, *previous.get_generated_clock_definition(), previous.get_comment());
      refreshed.copyAnnotations(previous);
      setClockSources(network, refreshed, previous.get_source_objects());
      previous = std::move(refreshed);
      ready.push_back(child);
    }
  }
  if (ready.size() != updated.size())
    throw std::invalid_argument("generated clock dependency cycle");
  std::vector<std::string> removed;
  for (const auto& [id, value] : _clock_map)
    if (!updated.contains(id))
      removed.push_back(id);
  if (removed.empty())
    _clock_map.swap(updated);
  else {
    // Clock deletion also changes consumers. Stage the whole affected SDC
    // transaction so selectors never refer to an already deleted clock.
    Sdc staged = *this;
    staged._clock_map = std::move(updated);
    for (const auto& id : removed)
      staged.removeClockReferences(id);
    *this = std::move(staged);
  }
}

void Sdc::removeClockReferences(const std::string& name)
{
  for (auto it = _clock_uncertainty_list.begin(); it != _clock_uncertainty_list.end();) {
    auto from = it->get_from_clocks(), to = it->get_to_clocks();
    const bool from_empty = from.erase(name) && from.empty();
    const bool to_empty = to.erase(name) && to.empty();
    if (from_empty || to_empty)
      it = _clock_uncertainty_list.erase(it);  // An empty explicit set is not a wildcard.
    else {
      it->set_from_clocks(std::move(from));
      it->set_to_clocks(std::move(to));
      ++it;
    }
  }
  for (auto it = _path_exception_list.begin(); it != _path_exception_list.end();) {
    auto from = it->get_from_objects(), to = it->get_to_objects();
    const ExceptionObject clock{ExceptionObjectType::kClock, name};
    const bool from_empty = from.erase(clock) && from.empty();
    const bool to_empty = to.erase(clock) && to.empty();
    if (from_empty || to_empty)
      it = _path_exception_list.erase(it);
    else {
      it->set_from_objects(from);
      it->set_to_objects(to);
      ++it;
    }
  }
  for (auto it = _clock_group_list.begin(); it != _clock_group_list.end();) {
    auto groups = it->get_groups();
    for (auto& group : groups)
      group.erase(name);
    std::erase_if(groups, [](const auto& group) { return group.empty(); });
    // One group means cut against all other clocks. Do not create that meaning
    // by deleting one side of a previously explicit two-group relationship.
    if (groups.empty() || (it->get_groups().size() > 1 && groups.size() < 2))
      it = _clock_group_list.erase(it);
    else {
      it->set_groups(groups);
      ++it;
    }
  }
  for (auto& [pin, constraint] : _port_constraint_map)
    constraint.removeClock(name);
  for (auto& [pin, selectors] : _clock_pin_constraints)
    selectors.erase(name);
  _electrical_limits.removeClock(name);
}
}  // namespace ista
