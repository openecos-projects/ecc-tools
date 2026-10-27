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
#include "RequiredPropagator.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace ista {

void RequiredPropagator::propagate()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kRequired);
  Database& database = _database;
  _required.clear();
  _setup_checks.clear();
  _through_index.rebuild(database.sdc(), database.network());
  const double required_time = resolveRequiredTime();

  seedEndPointRequired(required_time);

  for (auto iter = database.graph().vertexOrder().rbegin(); iter != database.graph().vertexOrder().rend(); ++iter) {
    for (std::size_t arc_idx : database.graph().vertices()[*iter].get_outgoing_arc_list()) {
      if (_query.isDisableArc(database.graph().arcs()[arc_idx])) {
        continue;
      }
      if (_query.shouldStopDataPropagation(database.graph().arcs()[arc_idx])) {
        continue;
      }
      propagateRequiredArc(database.graph().arcs()[arc_idx]);
    }
  }

  updateSlack();
}

double RequiredPropagator::resolveRequiredTime()
{
  Database& database = _database;
  double worst_arrival = 0.0;
  for (const std::string& end_point : database.graph().endpoints()) {
    TimingPoint& timing_point = database.graph().points()[end_point];
    if (_query.isFinite(timing_point.get_arrival())) {
      worst_arrival = std::max(worst_arrival, timing_point.get_arrival());
    }
  }
  return worst_arrival;
}

void RequiredPropagator::seedEndPointRequired(double required_time)
{
  Database& database = _database;
  for (const std::string& end_point : database.graph().endpoints()) {
    TimingPoint& point = database.graph().points()[end_point];
    auto mode = point.get_path_state_map().find(AnalysisType::kMax);
    if (mode == point.get_path_state_map().end()) {
      point.set_required(required_time);
      continue;
    }
    for (auto& [source, edges] : mode->second)
      for (auto& [edge, states] : edges)
        for (auto& [tag, state] : states) {
          if (auto end = _check.checkPath(state, end_point, AnalysisType::kMax)) {
            _required[&state] = end->required();
            _setup_checks[&state] = end->check().type == TimingCheckType::kSetup ? end->check().check_time : 0.0;
          } else if (!_query.isConstrainedEndPoint(end_point))
            _required[&state] = required_time;
        }
  }
}

void RequiredPropagator::propagateRequiredArc(Arc& arc)
{
  Database& database = _database;
  if (_query.isDisableArc(arc)) {
    return;
  }
  if (_query.shouldStopDataPropagation(arc)) {
    return;
  }
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  const auto source_mode = source_point.get_path_state_map().find(AnalysisType::kMax);
  const auto sink_mode = sink_point.get_path_state_map().find(AnalysisType::kMax);
  if (source_mode == source_point.get_path_state_map().end() || sink_mode == sink_point.get_path_state_map().end())
    return;
  ExceptionMatcher matcher(database.sdc(), database.network());
  for (auto& [source, edges] : source_mode->second) {
    const auto sink_source = sink_mode->second.find(source);
    if (sink_source == sink_mode->second.end())
      continue;
    for (auto& [input_edge, states] : edges)
      for (auto output_edge : _query.getOutputTransTypeList(arc, AnalysisType::kMax, input_edge)) {
        const auto sink_edge = sink_source->second.find(output_edge);
        if (sink_edge == sink_source->second.end())
          continue;
        const double delay = _query.getDataArcDelay(arc, AnalysisType::kMax, input_edge, output_edge);
        for (auto& [tag, state] : states) {
          if (state.get_is_clock_source_path() && arc.get_type() == ArcType::kNet
              && !database.sdc().clockAllowsNetArc(database.network(), state.get_clock_name(), arc.get_source_pin(), arc.get_sink_pin()))
            continue;
          const auto progress = matcher.advanceState(_through_index, state.get_exception_state_list(), arc.get_sink_pin(), output_edge,
                                                     arc.get_type() == ArcType::kNet ? arc.get_source_pin() : "");
          const auto target_tag = matcher.makePathStateTag(state.get_clock_name(), state.get_launch_clock_trans_type(), progress,
                                                           state.get_is_clock_source_path(), state.crprPathTag(), state.get_launch_clock_edge_index());
          const auto target = sink_edge->second.find(target_tag);
          if (target == sink_edge->second.end())
            continue;
          const auto required = _required.find(&target->second);
          if (required == _required.end())
            continue;
          const double candidate = required->second - delay;
          const auto current = _required.find(&state);
          if (current == _required.end() || candidate < current->second)
            _required[&state] = candidate;
        }
      }
  }
}

void RequiredPropagator::updateSlack()
{
  Database& database = _database;
  for (GraphVertex& vertex : database.graph().vertices()) {
    TimingPoint& timing_point = vertex.get_timing_point();
    const TimingPathState* worst = nullptr;
    double slack = std::numeric_limits<double>::infinity(), required = 0.0;
    const auto mode = timing_point.get_path_state_map().find(AnalysisType::kMax);
    if (mode != timing_point.get_path_state_map().end())
      for (const auto& [source, edges] : mode->second)
        for (const auto& [edge, states] : edges)
          for (const auto& [tag, state] : states) {
            const auto value = _required.find(&state);
            if (value != _required.end() && value->second - state.get_arrival() < slack) {
              slack = value->second - state.get_arrival();
              required = value->second;
              worst = &state;
            }
          }
    if (worst) {
      timing_point.set_arrival(worst->get_arrival());
      timing_point.set_required(required);
      timing_point.set_slack(slack);
      timing_point.set_launch_time(worst->get_launch_time());
      timing_point.setLaunchClock(worst->get_clock_name());
      if (auto check = _setup_checks.find(worst); check != _setup_checks.end())
        timing_point.set_setup_check_time(check->second);
    }
  }
}

}  // namespace ista
