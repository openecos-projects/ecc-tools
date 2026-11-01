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
#include "ClockPropagator.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ClockNetworkBuilder.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "graph/TimingStateQuery.hpp"
#include "network/Network.hpp"

namespace ista {
void ClockPropagator::propagate()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kClockPropagation);
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  auto sources = ClockNetworkBuilder(_database).build();
  propagateSources(sources);
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void ClockPropagator::propagateSources(std::vector<ClockSource>& sources)
{
  Database& database = _database;
  for (ClockSource& clock : sources) {
    _generated_clock_seeder.seed(clock);
    propagateClockSlewDelay(clock);
    for (int32_t vertex_idx : database.graph().vertexOrder()) {
      GraphVertex& vertex = database.graph().vertices()[vertex_idx];
      std::string& pin_name = vertex.get_pin_name();
      if (TimingPoint& timing_point = vertex.get_timing_point();
          !timing_point.has_clock_state(clock.get_clock_name()) || ClockNetworkBuilder(_database).stopsAt(pin_name)) {
        continue;
      }
      for (const auto& arc_idx : vertex.get_outgoing_arc_list()) {
        if (_clock_arc_propagator.isDisableArc(database.graph().arcs()[arc_idx])) {
          continue;
        }
        _clock_arc_propagator.propagateArrival(clock, arc_idx, AnalysisType::kMax);
        _clock_arc_propagator.propagateArrival(clock, arc_idx, AnalysisType::kMin);
      }
    }
    publishClock(clock);
  }
}

void ClockPropagator::propagateClockSlewDelay(ClockSource& clock)
{
  Database& database = _database;
  for (int32_t vertex_idx : database.graph().vertexOrder()) {
    GraphVertex& vertex = database.graph().vertices()[vertex_idx];
    std::string& pin_name = vertex.get_pin_name();
    TimingPoint& timing_point = vertex.get_timing_point();
    if (!timing_point.has_clock_state(clock.get_clock_name()) || ClockNetworkBuilder(_database).stopsAt(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : vertex.get_outgoing_arc_list()) {
      if (_clock_arc_propagator.isDisableArc(database.graph().arcs()[arc_idx])) {
        continue;
      }
      _clock_arc_propagator.propagateSlew(clock, arc_idx, AnalysisType::kMax);
      _clock_arc_propagator.propagateSlew(clock, arc_idx, AnalysisType::kMin);
    }
  }
}
void ClockPropagator::publishClock(ClockSource& clock)
{
  Database& database = _database;
  TimingClock& definition = database.sdc().get_clock_map().at(std::string(clock.get_clock_name()));
  for (GraphVertex& vertex : database.graph().vertices()) {
    TimingPoint& timing_point = vertex.get_timing_point();
    if (timing_point.find_clock_state(clock.get_clock_name()) == nullptr) {
      continue;
    }
    ClockState& clock_state = timing_point.get_clock_state(clock.get_clock_name());
    for (auto& [source_edge, state] : clock_state.waveforms) {
      state.arrival_map.clear();
      state.slew_map.clear();
      state.predecessor_map.clear();
      state.predecessor_arc_delay_map.clear();
      state.predecessor_trans_type_map.clear();
      for (AnalysisType analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
        for (TransType edge : {TransType::kRise, TransType::kFall}) {
          const auto physical = state.physical_arrival_map.find(analysis);
          if (physical == state.physical_arrival_map.end() || !physical->second.contains(edge))
            continue;
          const auto values
              = clockPinValues(database, vertex.get_pin_name(), clock.get_clock_name(), analysis, edge, AnalysisType::kNone, source_edge, &timing_point);
          state.arrival_map[analysis][edge] = values.source_latency + (values.propagated ? physical->second.at(edge) : values.network_latency);
          if (values.propagated) {
            state.slew_map[analysis][edge] = state.physical_slew_map[analysis][edge];
            if (state.physical_predecessor_map[analysis].contains(edge)) {
              state.predecessor_map[analysis][edge] = state.physical_predecessor_map[analysis][edge];
              state.predecessor_arc_delay_map[analysis][edge] = state.physical_predecessor_arc_delay_map[analysis][edge];
              state.predecessor_trans_type_map[analysis][edge] = state.physical_predecessor_trans_type_map[analysis][edge];
            }
          } else {
            const auto mode = definition.get_transition_map().find(analysis);
            state.slew_map[analysis][edge]
                = mode != definition.get_transition_map().end() && mode->second.contains(source_edge) ? mode->second.at(source_edge) : 0.0;
          }
        }
      }
    }
    clock_state.summarize();
  }
}
}  // namespace ista
