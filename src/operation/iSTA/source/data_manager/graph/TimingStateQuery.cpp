// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "graph/TimingStateQuery.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TimingTolerance.hpp"

namespace ista {

bool TimingStateQuery::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

bool TimingStateQuery::shouldStopDataPropagation(Arc& arc)
{
  return arc.get_type() == ArcType::kNet && isSequentialClockPin(arc.get_sink_pin());
}

bool TimingStateQuery::isSequentialClockPin(const std::string& pin_name)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[pin_name];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_clock_pin_name();
}

double TimingStateQuery::getClockArrival(const std::string& pin_name, AnalysisType analysis_type)
{
  return getClockArrival(pin_name, analysis_type, TransType::kRise);
}

double TimingStateQuery::getClockArrival(const std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (database.graph().points().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.graph().points()[pin_name];
  return getClockArrival(timing_point, analysis_type, trans_type);
}

double TimingStateQuery::getClockArrival(const std::string& pin_name, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                         std::optional<TransType> source)
{
  const ClockWaveform* state = clockWaveform(_database, pin_name, clock_name, source);
  return state == nullptr ? 0.0 : state->arrival(analysis_type, trans_type).value_or(0.0);
}

double TimingStateQuery::getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  const auto* state = timing_point.findDefaultClockState();
  return state == nullptr ? 0.0 : state->arrival(analysis_type, trans_type).value_or(0.0);
}

double TimingStateQuery::getClockSlew(const std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (database.graph().points().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.graph().points()[pin_name];
  const auto* state = timing_point.findDefaultClockState();
  return state == nullptr ? 0.0 : state->slew(analysis_type, trans_type).value_or(0.0);
}

double TimingStateQuery::getClockSlew(const std::string& pin_name, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                      std::optional<TransType> source)
{
  const ClockWaveform* state = clockWaveform(_database, pin_name, clock_name, source);
  return state == nullptr ? 0.0 : state->slew(analysis_type, trans_type).value_or(0.0);
}

const TimingPoint* clockPoint(const Database& database, const std::string& pin, std::string_view clock)
{
  const auto receiver = database.graph().points().find(pin);
  if (receiver != database.graph().points().end() && receiver->second.find_clock_state(clock))
    return &receiver->second;
  const auto driver = database.graph().driverPoints().find(pin);
  return driver != database.graph().driverPoints().end() && driver->second.find_clock_state(clock) ? &driver->second : nullptr;
}

const ClockWaveform* clockWaveform(const Database& database, const std::string& pin, std::string_view clock, std::optional<TransType> source)
{
  const auto* point = clockPoint(database, pin, clock);
  return point ? point->find_clock_state(clock)->waveform(source) : nullptr;
}

std::vector<TransType> clockSourceTransitions(const Database& database, const std::string& pin, std::string_view clock, AnalysisType mode, TransType local_edge)
{
  const auto* point = clockPoint(database, pin, clock);
  if (!point)
    return {local_edge};  // Virtual or unconstrained clock.
  std::vector<TransType> result;
  for (const auto& [source, wave] : point->find_clock_state(clock)->waveforms)
    if (wave.arrival(mode, local_edge))
      result.push_back(source);
  return result;
}

bool isIdealClockPin(const Database& database, const std::string& pin)
{
  auto ideal = [&](const TimingPoint& point) {
    for (const auto& [clock, state] : point.get_clock_state_map()) {
      if (state.is_source_path_only || !database.sdc().get_clock_map().contains(clock))
        continue;
      for (const auto& [source, wave] : state.waveforms)
        for (const auto& [analysis, edges] : wave.physical_slew_map)
          for (const auto& [edge, slew] : edges)
            if (!clockPinValues(database, pin, clock, analysis, edge, AnalysisType::kNone, source, &point).propagated)
              return true;
    }
    return false;
  };
  const auto receiver = database.graph().points().find(pin);
  const auto driver = database.graph().driverPoints().find(pin);
  return (receiver != database.graph().points().end() && ideal(receiver->second)) || (driver != database.graph().driverPoints().end() && ideal(driver->second));
}

std::vector<TimingIoDelay> resolveIoDelays(const Database& database, const std::vector<const TimingIoDelay*>& definitions)
{
  std::vector<TimingIoDelay> result;
  for (const auto* delay : definitions) {
    if (delay->get_reference_pin().empty()) {
      result.push_back(*delay);
      continue;
    }
    std::set<std::string> clocks;
    for (const auto* points : {&database.graph().points(), &database.graph().driverPoints()}) {
      const auto point = points->find(delay->get_reference_pin());
      if (point == points->end())
        continue;
      for (const auto& [clock, state] : point->second.get_clock_state_map()) {
        if (state.is_source_path_only || (!delay->get_clock_name().empty() && clock != delay->get_clock_name()))
          continue;
        clocks.insert(clock);
      }
    }
    if (clocks.empty())
      throw std::invalid_argument("I/O reference pin '" + delay->get_reference_pin() + "' is not on the selected clock network '" + delay->get_clock_name()
                                  + "'");
    for (const auto& clock : clocks) {
      result.push_back(*delay);
      result.back().set_clock_name(clock);
    }
  }
  return result;
}

ClockPinValues clockPinValues(const Database& database, const std::string& pin, std::string_view clock_name, AnalysisType analysis, TransType transition,
                              AnalysisType check, std::optional<TransType> source_edge, const TimingPoint* point)
{
  ClockPinValues result;
  const auto definition = database.sdc().get_clock_map().find(std::string(clock_name));
  if (definition == database.sdc().get_clock_map().end())
    return result;
  const auto& clock = definition->second;
  result.propagated = clock.get_is_propagated();
  result.source_latency = clock.get_source_latency(analysis, clockVariation(analysis), source_edge.value_or(transition));
  result.network_latency = clock.get_network_latency(analysis, source_edge.value_or(transition));
  result.uncertainty = check == AnalysisType::kMin ? clock.get_hold_uncertainty() : clock.get_setup_uncertainty();
  const auto& annotations = database.sdc().clockPinConstraints();
  if (annotations.empty())
    return result;
  bool mode_found = false, source_found = false, network_found = false, uncertainty_found = false;
  auto trace_edge = transition;
  auto apply = [&](const ClockPinConstraint& annotation, const std::string& name) {
    if (!mode_found && annotation.propagated) {
      result.propagated = *annotation.propagated;
      mode_found = true;
      if (result.propagated)
        result.propagated_pin = name;
    }
    auto source = annotation.source_latency.find(analysis);
    if (!source_found && source != annotation.source_latency.end()) {
      auto variation = source->second.find(clockVariation(analysis));
      if (variation != source->second.end() && variation->second.contains(trace_edge)) {
        result.source_latency = variation->second.at(trace_edge);
        source_found = true;
      }
    }
    auto network = annotation.network_latency.find(analysis);
    if (!network_found && network != annotation.network_latency.end() && network->second.contains(trace_edge)) {
      result.network_latency = network->second.at(trace_edge);
      network_found = true;
    }
    const auto uncertainty = check == AnalysisType::kMin ? annotation.hold_uncertainty : annotation.setup_uncertainty;
    if (!uncertainty_found && uncertainty) {
      result.uncertainty = *uncertainty;
      uncertainty_found = true;
    }
  };
  std::string current = pin;
  if (!point)
    point = clockPoint(database, pin, clock_name);
  // A physical predecessor is unique for this clock, analysis and edge. Bound
  // traversal as a guard against malformed traces without allocating a set.
  for (size_t remaining = database.graph().vertices().size() + 1; remaining && !current.empty(); --remaining) {
    auto entry = annotations.find(current);
    if (entry != annotations.end()) {
      auto specific = entry->second.find(std::string(clock_name));
      if (specific != entry->second.end())
        apply(specific->second, current);
      auto generic = entry->second.find("");
      if (generic != entry->second.end())
        apply(generic->second, current);
    }
    const auto* clock_state = point ? point->find_clock_state(clock_name) : nullptr;
    const auto* state = clock_state ? clock_state->waveform(source_edge) : nullptr;
    if (!state)
      break;
    auto pred = state->physical_predecessor_map.find(analysis);
    if (pred == state->physical_predecessor_map.end() || !pred->second.contains(trace_edge))
      break;
    current = pred->second.at(trace_edge);
    const auto vertices = state->predecessor_vertex_map.find(analysis);
    if (vertices != state->predecessor_vertex_map.end() && vertices->second.contains(trace_edge)) {
      const auto index = vertices->second.at(trace_edge);
      point = index >= 0 && static_cast<size_t>(index) < database.graph().vertices().size() ? &database.graph().vertices()[index].get_timing_point() : nullptr;
    } else {
      point = clockPoint(database, current, clock_name);
    }
    if (source_edge)
      source_edge = state->predecessorSource(analysis, trace_edge, *source_edge);
    auto edges = state->physical_predecessor_trans_type_map.find(analysis);
    if (edges != state->physical_predecessor_trans_type_map.end() && edges->second.contains(trace_edge))
      trace_edge = edges->second.at(trace_edge);
  }
  return result;
}

const ClockPinValues& ClockPinValuesQuery::values(const std::string& pin, std::string_view clock, AnalysisType analysis, TransType transition,
                                                 AnalysisType check, std::optional<TransType> source)
{
  if (_mode == CacheMode::kNone) {
    _live = clockPinValues(_database, pin, clock, analysis, transition, check, source);
    return _live;
  }
  const auto key = std::tuple{std::string_view(pin), clock, analysis, transition, check, source};
  if (const auto found = _values.find(key); found != _values.end())
    return found->second;
  return _values.emplace(Key{pin, clock, analysis, transition, check, source},
                         clockPinValues(_database, pin, clock, analysis, transition, check, source)).first->second;
}

double TimingStateQuery::clockPathBound(const std::string& pin_name, std::string_view clock_name, TransType transition, std::optional<TransType> source) const
{
  double bound = 0.;
  // Equal arrival at a leaf does not imply equal arrivals on its common prefix.
  // Inspect both selected early/late traces; flat ideal clocks keep one tag.
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    const auto* point = clockPoint(_database, pin_name, clock_name);
    auto edge = transition;
    auto origin = source;
    std::set<const TimingPoint*> visited;
    while (point && visited.insert(point).second) {
      const auto* clock = point->find_clock_state(clock_name);
      const auto* state = clock ? clock->waveform(origin) : nullptr;
      if (!state)
        break;
      const auto early = state->arrival(AnalysisType::kMin, edge);
      const auto late = state->arrival(AnalysisType::kMax, edge);
      if (early && late && std::isfinite(*early) && std::isfinite(*late) && *early != *late)
        bound = std::max(bound, std::abs(*late - *early));
      const auto predecessors = state->predecessor_vertex_map.find(mode);
      if (predecessors == state->predecessor_vertex_map.end() || !predecessors->second.contains(edge))
        break;
      const auto vertex = predecessors->second.at(edge);
      if (vertex < 0 || static_cast<std::size_t>(vertex) >= _database.graph().vertices().size())
        break;
      point = &_database.graph().vertices()[vertex].get_timing_point();
      if (origin)
        origin = state->predecessorSource(mode, edge, *origin);
      const auto edges = state->predecessor_trans_type_map.find(mode);
      if (edges != state->predecessor_trans_type_map.end() && edges->second.contains(edge))
        edge = edges->second.at(edge);
    }
  }
  return bound;
}

bool TimingStateQuery::clockPathHasVariation(const std::string& pin_name, std::string_view clock_name, TransType transition,
                                             std::optional<TransType> source) const
{
  return clockPathBound(pin_name, clock_name, transition, source) > 0.;
}

bool TimingStateQuery::canPrunePathStates() const
{
  const auto& sdc = _database.sdc();
  // Local clock annotations still require exhaustive state propagation.
  if (!sdc.clockPinConstraints().empty() || sdc.get_clock_map().empty() || _database.graph().endpoints().empty())
    return false;
  for (const auto& vertex : _database.graph().vertices())
    for (const auto& [clock, state] : vertex.get_timing_point().get_clock_state_map())
      for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
        // Keep the local launch transition unique within a source-edge tag.
        for (const auto& [source, wave] : state.waveforms)
          if (wave.arrival(mode, TransType::kRise) && wave.arrival(mode, TransType::kFall))
            return false;
        for (auto edge : {TransType::kRise, TransType::kFall}) {
          int count = 0;
          for (const auto& [source, wave] : state.waveforms)
            count += wave.arrival(mode, edge).has_value();
          if (count > 1)
            return false;
        }
      }
  return true;
}

TimingStateQuery::PathStateGroup TimingStateQuery::pathStateGroup(const TimingPathState& state, const std::vector<int32_t>* progress) const
{
  const bool exceptions = !_database.sdc().get_path_exception_list().empty();
  const auto start_class = exceptions ? state.exceptionStartClass() : 0;
  // Direct/synthetic callers without forward classification remain separate.
  return {state.group(), progress ? *progress : state.get_exception_state_list(), start_class,
          exceptions && start_class == 0 ? state.get_start_point() : std::string{}};
}

bool TimingStateQuery::prunePathStates(TimingPoint& point, std::size_t remaining_arcs, const std::vector<std::string_view>* allowed_clocks) const
{
  bool pruned = false;
  auto allowed = [&](const TimingPathState& state) {
    return !allowed_clocks || std::find(allowed_clocks->begin(), allowed_clocks->end(), state.get_clock_name()) != allowed_clocks->end();
  };
  // isBetterArrival can retain a same-tag predecessor up to one tolerance
  // behind the witness at every later merge. Cover the entire continuation,
  // plus endpoint comparison/rounding, before discarding another launch tag.
  const double margin = (static_cast<double>(remaining_arcs) + 4.) * kTimingTolerance;
  for (auto& [mode, sources] : point.get_path_state_map())
    for (auto& [source, edges] : sources)
      for (auto& [edge, states] : edges) {
        // A directly seeded state can carry a different slew. Its endpoint
        // check need not match the physical slew of arriving states.
        if (!states.empty() && std::any_of(states.begin(), states.end(), [&](const auto& item) {
              return item.second.get_slew() != states.begin()->second.get_slew();
            }))
          continue;
        std::map<PathStateGroup, double> witnesses;
        for (const auto& [tag, state] : states) {
          if (!allowed(state))
            continue;
          const double witness = state.get_arrival() + (mode == AnalysisType::kMax ? -state.clockPathBound() : state.clockPathBound());
          if (!std::isfinite(witness))
            continue;
          auto [it, inserted] = witnesses.emplace(pathStateGroup(state), witness);
          if (!inserted)
            it->second = mode == AnalysisType::kMax ? std::max(it->second, witness) : std::min(it->second, witness);
        }
        for (auto it = states.begin(); it != states.end();) {
          if (!allowed(it->second)) {
            ++it;
            continue;
          }
          const auto found = witnesses.find(pathStateGroup(it->second));
          const double arrival = it->second.get_arrival();
          if (found != witnesses.end() && (mode == AnalysisType::kMax ? found->second > arrival + margin : found->second < arrival - margin)) {
            it = states.erase(it);
            pruned = true;
          } else
            ++it;
        }
      }
  return pruned;
}

double TimingStateQuery::roundTime(double time)
{
  return std::round(time * 1E15) / 1E15;
}

std::vector<TransType> TimingStateQuery::getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
{
  std::vector<TransType> output_trans_type_list;
  if (arc.get_input_output_delay_map().count(analysis_type) == 0 || arc.get_input_output_delay_map()[analysis_type].count(input_trans_type) == 0) {
    return output_trans_type_list;
  }
  for (std::pair<const TransType, double>& delay_pair : arc.get_input_output_delay_map()[analysis_type][input_trans_type]) {
    output_trans_type_list.push_back(delay_pair.first);
  }
  return output_trans_type_list;
}

double TimingStateQuery::getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (arc.rcFailed(analysis_type, input_trans_type))
    return 0.;
  if (arc.get_trans_delay_map().count(analysis_type) > 0 && arc.get_trans_delay_map()[analysis_type].count(input_trans_type) > 0) {
    return arc.get_trans_delay_map()[analysis_type][input_trans_type];
  }
  if (analysis_type == AnalysisType::kMin) {
    return arc.get_delay_min();
  }
  return arc.get_delay_max();
}

double TimingStateQuery::getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  if (arc.rcFailed(analysis_type, output_trans_type))
    return 0.;
  if (arc.get_graph_delay_map().count(analysis_type) > 0 && arc.get_graph_delay_map()[analysis_type].count(input_trans_type) > 0
      && arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) > 0) {
    return arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type];
  }
  if (arc.get_input_output_delay_map().count(analysis_type) > 0 && arc.get_input_output_delay_map()[analysis_type].count(input_trans_type) > 0
      && arc.get_input_output_delay_map()[analysis_type][input_trans_type].count(output_trans_type) > 0) {
    return arc.get_input_output_delay_map()[analysis_type][input_trans_type][output_trans_type];
  }
  return getArcDelay(arc, analysis_type, input_trans_type);
}

double TimingStateQuery::getDataArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  return deratedDelay(getArcDelay(arc, analysis_type, input_trans_type, output_trans_type), analysis_type, false, output_trans_type,
                      arc.get_type() == ArcType::kNet ? DerateDelay::kNet : DerateDelay::kCell, arc.get_source_pin());
}

DerateContext TimingStateQuery::derateContext(const std::string& pin_name) const
{
  DerateContext context;
  const auto& network = _database.network();
  const auto pin = network.pins().find(pin_name);
  if (pin == network.pins().end())
    return context;
  context.net = pin->second.get_net_name();
  if (pin->second.get_is_port())
    return context;
  context.instance = pin->second.get_instance_name();
  const auto instance = network.instances().find(std::string(context.instance));
  if (instance != network.instances().end()) {
    const auto& cells = _database.liberty().get_cell_map();
    const auto cell = cells.find(instance->second.get_cell_name());
    if (cell != cells.end())
      context.lib_cell = cell->second.get_library_name() + "/" + cell->first;
  }
  return context;
}

double TimingStateQuery::deratedDelay(double nominal, AnalysisType analysis, bool clock_path, TransType edge, DerateDelay kind,
                                      const std::string& pin_name) const
{
  return _database.sdc().timingDerates().apply(nominal, analysis, clock_path, edge, kind, derateContext(pin_name));
}

double TimingStateQuery::maxDataArcDelay(Arc& arc)
{
  const auto kind = arc.get_type() == ArcType::kNet ? DerateDelay::kNet : DerateDelay::kCell;
  const auto& derates = _database.sdc().timingDerates();
  const auto context = derateContext(arc.get_source_pin());
  const bool rc_failed = arc.rcFailed(AnalysisType::kMax, TransType::kRise) || arc.rcFailed(AnalysisType::kMax, TransType::kFall);
  if (!rc_failed && derates.factor(AnalysisType::kMax, false, TransType::kRise, kind, context) == 1.
      && derates.factor(AnalysisType::kMax, false, TransType::kFall, kind, context) == 1.)
    return arc.get_delay();
  std::optional<double> worst;
  for (auto input : {TransType::kRise, TransType::kFall})
    for (auto output : getOutputTransTypeList(arc, AnalysisType::kMax, input)) {
      const double delay = getDataArcDelay(arc, AnalysisType::kMax, input, output);
      if (!worst || delay > *worst)
        worst = delay;
    }
  return worst.value_or(rc_failed ? 0. : arc.get_delay());
}

bool TimingStateQuery::hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type)
{
  return hasPathState(timing_point, analysis_type, source_type, TransType::kRise) || hasPathState(timing_point, analysis_type, source_type, TransType::kFall);
}

bool TimingStateQuery::hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type)
{
  return timing_point.get_path_state_map().count(analysis_type) > 0 && timing_point.get_path_state_map()[analysis_type].count(source_type) > 0
         && timing_point.get_path_state_map()[analysis_type][source_type].count(trans_type) > 0
         && !timing_point.get_path_state_map()[analysis_type][source_type][trans_type].empty();
}

std::map<std::string, TimingPathState>& TimingStateQuery::getPathStateMap(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type,
                                                                          TransType trans_type)
{
  return timing_point.get_path_state_map()[analysis_type][source_type][trans_type];
}

TimingPathState& TimingStateQuery::getPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                                const std::string& path_state_tag)
{
  return timing_point.get_path_state_map()[analysis_type][source_type][trans_type][path_state_tag];
}

TimingPathState* TimingStateQuery::getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type)
{
  TimingPathState* best_path_state = nullptr;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    TimingPathState* path_state = getWorstPathState(timing_point, analysis_type, source_type, trans_type);
    if (path_state == nullptr) {
      continue;
    }
    if (best_path_state == nullptr || isBetterArrival(path_state->get_arrival(), best_path_state->get_arrival(), analysis_type)) {
      best_path_state = path_state;
    }
  }
  return best_path_state;
}

TimingPathState* TimingStateQuery::getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type)
{
  if (!hasPathState(timing_point, analysis_type, source_type, trans_type)) {
    return nullptr;
  }

  TimingPathState* best_path_state = nullptr;
  std::map<std::string, TimingPathState>& path_state_map = getPathStateMap(timing_point, analysis_type, source_type, trans_type);
  for (std::pair<const std::string, TimingPathState>& path_state_pair : path_state_map) {
    TimingPathState& path_state = path_state_pair.second;
    if (!isFinite(path_state.get_arrival())) {
      continue;
    }
    if (best_path_state == nullptr || isBetterArrival(path_state.get_arrival(), best_path_state->get_arrival(), analysis_type)) {
      best_path_state = &path_state;
    }
  }
  return best_path_state;
}

TransType TimingStateQuery::getEndPointTransType(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type)
{
  TransType best_trans_type = TransType::kNone;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    TimingPathState* path_state = getWorstPathState(timing_point, analysis_type, source_type, trans_type);
    if (path_state == nullptr) {
      continue;
    }
    if (best_trans_type == TransType::kNone
        || isBetterArrival(path_state->get_arrival(), getWorstPathState(timing_point, analysis_type, source_type, best_trans_type)->get_arrival(),
                           analysis_type)) {
      best_trans_type = trans_type;
    }
  }
  return best_trans_type;
}

bool TimingStateQuery::isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type)
{
  if (!isFinite(current_arrival)) {
    return true;
  }
  if (analysis_type == AnalysisType::kMin) {
    return candidate_arrival < current_arrival - ista::kTimingTolerance;
  }
  return candidate_arrival > current_arrival + ista::kTimingTolerance;
}

bool TimingStateQuery::isFinite(double value)
{
  return std::isfinite(value);
}

double TimingStateQuery::getDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (timing_point.get_data_slew_map().count(analysis_type) == 0 || timing_point.get_data_slew_map()[analysis_type].count(trans_type) == 0) {
    return 0.0;
  }
  return timing_point.get_data_slew_map()[analysis_type][trans_type];
}

bool TimingStateQuery::isConstrainedEndPoint(const std::string& end_point)
{
  return isOutputEndPoint(end_point) || isRegisterEndPoint(end_point) || isTimingCheckEndPoint(end_point);
}

bool TimingStateQuery::isOutputEndPoint(const std::string& end_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[end_point];
  if (pin.get_is_port() && getStartPointClock(end_point) != nullptr && database.graph().outgoingArcs()[end_point].empty()) {
    return false;
  }
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout) && hasOutputDelay(end_point);
}

bool TimingStateQuery::hasOutputDelay(const std::string& end_point)
{
  Database& database = _database;
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
  if (port_constraint_map.count(end_point) == 0) {
    return false;
  }
  TimingPortConstraint& port_constraint = port_constraint_map[end_point];
  return port_constraint.get_has_output_delay_max() || port_constraint.get_has_output_delay_min();
}

bool TimingStateQuery::isRegisterEndPoint(const std::string& end_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[end_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && hasClockPoint(instance.get_clock_pin_name()) && end_point == instance.get_data_pin_name();
}

bool TimingStateQuery::isTimingCheckEndPoint(const std::string& end_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[end_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || !hasClockPoint(instance.get_clock_pin_name())) {
    return false;
  }
  for (TimingCheckArc& timing_check_arc : instance.get_check_arc_list()) {
    if (timing_check_arc.get_data_port() == end_point) {
      return true;
    }
  }
  return false;
}

TimingClock* TimingStateQuery::getStartPointClock(const std::string& start_point)
{
  Database& database = _database;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.sdc().get_clock_map()) {
    for (std::string& clock_source : clock_pair.second.get_source_list()) {
      if (clock_source == start_point) {
        return &clock_pair.second;
      }
    }
  }
  return nullptr;
}

bool TimingStateQuery::hasClockPoint(const std::string& pin_name)
{
  Database& database = _database;
  return database.graph().points().count(pin_name) > 0 && database.graph().points()[pin_name].get_is_clock_point();
}

TimingPathState* TimingStateQuery::getWorstEndPointPathState(const std::string& end_point, AnalysisType analysis_type)
{
  Database& database = _database;
  TimingPathState* end_path_state = getWorstPathState(database.graph().points()[end_point], analysis_type, PathSourceType::kInput);
  if (end_path_state == nullptr) {
    end_path_state = getWorstPathState(database.graph().points()[end_point], analysis_type, PathSourceType::kRegister);
  }
  return end_path_state;
}

}  // namespace ista
