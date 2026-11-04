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
#include "GeneratedClockSeeder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Logger.hpp"
#include "network/Network.hpp"

namespace ista {
void GeneratedClockSeeder::seed(ClockSource& clock)
{
  Database& database = _database;
  const TimingClock& definition = database.sdc().get_clock_map().at(std::string(clock.get_clock_name()));
  if (definition.get_is_generated() && clock.get_is_propagated() && seedFromMaster(clock, definition)) {
    // A generated clock defines a new waveform at its target. Retain the
    // insertion trace, but rebase source-edge identity to that target's edges.
    for (const auto& pin : clock.get_source_list()) {
      auto& state = database.graph().driverPoint(pin).get_clock_state(clock.get_clock_name());
      for (auto edge : {TransType::kRise, TransType::kFall})
        state.waveforms[edge] = state.physicalEdge(edge);
      state.summarize();
    }
    return;
  }
  if (definition.get_is_generated() && clock.get_is_propagated()) {
    if (database.network().hierarchyPins().contains(definition.get_master_source()))
      throw std::invalid_argument("generated clock '" + std::string(clock.get_clock_name())
                                  + "' has no valid path from its master through hierarchical source '" + definition.get_master_source() + "'");
    STALOG.warn(Loc::current(), "generated clock '", clock.get_clock_name(), "' has no timing path from master clock '", definition.get_master_clock_name(),
                "' to its target; using zero insertion delay");
  }
  for (const auto& pin_name : clock.get_source_list()) {
    if (!database.graph().points().contains(pin_name)) {
      continue;
    }
    TimingPoint& timing_point = database.graph().driverPoint(pin_name);
    ClockState& state = timing_point.get_clock_state(clock.get_clock_name());
    const bool port = database.network().pins().at(pin_name).get_is_port();
    const auto constraint = database.sdc().get_port_constraint_map().find(pin_name);
    // A remaining explicit port transition selects a fixed source waveform in
    // PT2018, even if a different lane retains a driving-cell annotation.
    const bool fixed_transition = constraint != database.sdc().get_port_constraint_map().end() && constraint->second.get_has_input_transition();
    for (auto edge : {TransType::kRise, TransType::kFall})
      for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
        auto pin = pin_name;
        const auto driver = port && !fixed_transition ? _delay.inputPortTiming(pin, mode, edge) : std::nullopt;
        state.waveforms[edge].physical_arrival_map[mode][edge] = driver ? driver->get_delay() : 0.;
        state.waveforms[edge].physical_slew_map[mode][edge] = driver ? driver->get_slew() : port ? _delay.inputPortSlew(pin, mode, edge) : 0.;
      }
    state.summarize();
  }
}

bool GeneratedClockSeeder::seedFromMaster(ClockSource& clock, const TimingClock& definition)
{
  Database& database = _database;
  const std::string& master_name = definition.get_master_clock_name();
  const std::string& master_source = definition.get_master_source();
  const bool combinational = definition.get_generated_clock_definition().has_value() && definition.get_generated_clock_definition()->combinational;
  std::set<int32_t> source_path_points;
  std::set<size_t> source_path_arcs;
  std::queue<int32_t> pin_queue;
  for (const std::string& target : clock.get_source_list()) {
    if (database.graph().points().contains(target))
      pin_queue.push(database.network().pins().at(target).get_driver_vertex_idx());
  }

  auto validSourceArc = [&](Arc& arc) {
    if (!database.network().hierarchyPins().contains(master_source))
      return true;
    const auto& boundary = database.network().checkedHierarchyPin(master_source);
    return database.network().crossesHierarchyPin(boundary, arc.get_source_pin(), arc.get_sink_pin())
           && database.sdc().clockAllowsNetArc(database.network(), master_name, arc.get_source_pin(), arc.get_sink_pin(),
                                               database.network().hierarchyCrossingPosition(boundary, arc.get_source_pin(), arc.get_sink_pin()));
  };
  auto isMasterSeed = [&](GraphVertex& vertex) {
    const std::string& pin = vertex.get_pin_name();
    if (!vertex.get_timing_point().has_clock_state(master_name) || !hasPhysicalClockState(vertex.get_timing_point(), master_name))
      return false;
    if (master_source.empty())
      return true;
    if (!database.sdc().matchesClockSource(database.network(), master_source, pin))
      return false;
    if (!database.network().hierarchyPins().contains(master_source))
      return true;
    // A hierarchy target can project to the same driver as -source. Even then,
    // verify a real, ordered crossing; a shared driver alone proves no path.
    std::vector<std::string> local_targets;
    for (const auto& target : definition.get_source_objects())
      if (database.sdc().matchesClockSource(database.network(), target, pin))
        local_targets.push_back(target);
    auto reaches = [&](const std::string& target) {
      if (!target.empty() && !database.network().hierarchyPins().contains(target))
        return false;
      for (auto arc_idx : vertex.get_outgoing_arc_list()) {
        auto& arc = database.graph().arcs()[arc_idx];
        if (arc.get_type() != ArcType::kNet || _clock_arc_propagator.isDisableArc(arc) || !validSourceArc(arc))
          continue;
        if (target.empty()) {
          if (source_path_arcs.contains(arc_idx))
            return true;
        } else if (database.network().hierarchyCrossingPosition(database.network().checkedHierarchyPin(target), pin, arc.get_sink_pin())
                   >= database.network().hierarchyCrossingPosition(database.network().checkedHierarchyPin(master_source), pin, arc.get_sink_pin()))
          return true;
      }
      return false;
    };
    if (local_targets.empty())
      return reaches("");
    return std::ranges::all_of(local_targets, reaches);
  };

  bool found_master = false;
  while (!pin_queue.empty()) {
    int32_t vertex_idx = pin_queue.front();
    GraphVertex& vertex = database.graph().vertices()[vertex_idx];
    pin_queue.pop();
    if (!source_path_points.insert(vertex_idx).second)
      continue;
    if (isMasterSeed(vertex)) {
      found_master = true;
      continue;
    }
    for (std::size_t arc_idx : vertex.get_incoming_arc_list()) {
      Arc& arc = database.graph().arcs()[arc_idx];
      if (_clock_arc_propagator.isDisableArc(arc) || (combinational && arc.get_is_clock_arc()))
        continue;
      if (database.network().hierarchyPins().contains(master_source)
          && database.sdc().matchesClockSource(database.network(), master_source, arc.get_source_pin()) && !validSourceArc(arc))
        continue;
      source_path_arcs.insert(arc_idx);
      pin_queue.push(arc.get_source_vertex_idx());
    }
  }
  if (!found_master)
    return false;

  for (int32_t vertex_idx : source_path_points) {
    GraphVertex& vertex = database.graph().vertices()[vertex_idx];
    TimingPoint& point = vertex.get_timing_point();
    const bool marked = point.has_clock_state(clock.get_clock_name());
    ClockState& generated_state = point.get_clock_state(clock.get_clock_name());
    generated_state.is_source_path_only = !marked;
    if (const ClockState* master_state = point.find_clock_state(master_name); master_state != nullptr && isMasterSeed(vertex)) {
      generated_state.waveforms = master_state->waveforms;
      generated_state.summarize();
    }
  }

  // Timing order normally resolves the source path in one pass. Repeating permits
  // clock-to-Q paths that cross a graph levelization boundary.
  for (std::size_t pass = 0; pass < source_path_points.size(); ++pass) {
    for (int32_t vertex_idx : database.graph().vertexOrder()) {
      if (!source_path_points.contains(vertex_idx))
        continue;
      GraphVertex& vertex = database.graph().vertices()[vertex_idx];
      TimingPoint& source_point = vertex.get_timing_point();
      if (!hasPhysicalClockState(source_point, clock.get_clock_name()))
        continue;
      for (std::size_t arc_idx : vertex.get_outgoing_arc_list()) {
        Arc& arc = database.graph().arcs()[arc_idx];
        if (!source_path_arcs.contains(arc_idx) || _clock_arc_propagator.isDisableArc(arc) || (combinational && arc.get_is_clock_arc()))
          continue;
        _clock_arc_propagator.propagateSlew(clock, arc_idx, AnalysisType::kMax, false);
        _clock_arc_propagator.propagateSlew(clock, arc_idx, AnalysisType::kMin, false);
        _clock_arc_propagator.propagateArrival(clock, arc_idx, AnalysisType::kMax, false);
        _clock_arc_propagator.propagateArrival(clock, arc_idx, AnalysisType::kMin, false);
      }
    }
  }

  return std::ranges::all_of(clock.get_source_list(), [&](const std::string& target) {
    return database.graph().points().contains(target) && hasPhysicalClockState(database.graph().driverPoint(target), clock.get_clock_name());
  });
}

bool GeneratedClockSeeder::hasPhysicalClockState(const TimingPoint& timing_point, std::string_view clock_name)
{
  const ClockState* state = timing_point.find_clock_state(clock_name);
  if (state == nullptr)
    return false;
  for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
    const auto analysis_iter = state->physical_arrival_map.find(analysis_type);
    if (analysis_iter != state->physical_arrival_map.end() && !analysis_iter->second.empty())
      return true;
  }
  return false;
}
}  // namespace ista
