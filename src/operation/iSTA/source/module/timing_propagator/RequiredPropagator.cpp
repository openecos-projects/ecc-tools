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
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "CaptureRequiredFrontier.hpp"
#include "ClockReconvergenceCalculator.hpp"
#include "TimingTolerance.hpp"
#include "data/TransitionFilter.hpp"

namespace ista {

void RequiredPropagator::propagate()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kRequired);
  Database& database = _database;
  _required.clear();
  _setup_checks.clear();
  _through_index.rebuild(database.sdc(), database.network());
  const double required_time = resolveRequiredTime();

  const bool capture_required = database.graph().pathStatesPruned();
  const bool exhaustive = !capture_required || database.graph().exhaustiveRequired();
  if (exhaustive) {
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
  }
  if (capture_required)
    propagateCaptureRequired(required_time);
  updateSlack();
}

void RequiredPropagator::propagateCaptureRequired(double required_time)
{
  using Candidates = detail::CaptureRequiredFrontier;
  using Groups = std::map<TimingStateQuery::PathStateGroup, Candidates>;
  using Edges = std::map<TransType, Groups>;
  using Sources = std::map<PathSourceType, Edges>;
  // Capture metadata is invariant along the data cone. Own each endpoint
  // check once; deque insertion keeps references stable for the whole pass.
  std::deque<PathCheck> checks;
  std::unordered_map<TimingPoint*, Sources> frontiers;
  std::unordered_set<const TimingPoint*> endpoints;
  // Index the immutable clock paths, rather than storing every launch/capture
  // pair. The index dies with this pass so the next propagate sees new clocks.
  ClockReconvergenceCalculator reconvergence(_database, ClockReconvergenceCalculator::CacheMode::kSnapshot);
  std::string common;
  const ExceptionMatcher matcher(_database.sdc(), _database.network());
  // Only these endpoint cones have the single-capture guarantee. Other cones
  // retain every launch state and use ordinary required propagation above.
  for (const auto& endpoint : _database.graph().endpoints()) {
    const auto& captures = _database.graph().captureRequiredClocks();
    if (!captures.empty() && !captures.contains(endpoint))
      continue;
    auto& point = _database.graph().points().at(endpoint);
    endpoints.insert(&point);
    const auto mode = point.get_path_state_map().find(AnalysisType::kMax);
    if (mode == point.get_path_state_map().end()) {
      point.set_required(required_time);
      continue;
    }
    for (const auto& [source, edges] : mode->second)
      for (const auto& [edge, states] : edges) {
        auto& groups = frontiers[&point][source][edge];
        for (const auto& [tag, state] : states) {
          if (!captures.empty() && !captures.at(endpoint).contains(state.get_clock_name()))
            continue;
          const auto group = _query.pathStateGroup(state);
          if (groups.contains(group))
            continue;
          auto& candidates = groups[group];
          if (const auto end = _check.checkPath(state, endpoint, AnalysisType::kMax, false)) {
            const auto& check = end->check();
            const double bound = _query.clockPathBound(check.capture_pin, check.capture_clock, check.capture_pin_transition, check.capture_transition);
            checks.push_back(check);
            candidates.insert({&checks.back(), end->required(), bound});
          } else if (!_query.isConstrainedEndPoint(endpoint)) {
            PathCheck check;
            check.required_before_round = required_time;
            checks.push_back(std::move(check));
            candidates.insert({&checks.back(), required_time, 0.});
          }
        }
      }
  }
  for (auto vertex = _database.graph().vertexOrder().rbegin(); vertex != _database.graph().vertexOrder().rend(); ++vertex) {
    auto& point = _database.graph().vertices()[*vertex].get_timing_point();
    auto& target = frontiers[&point];
    const auto mode = point.get_path_state_map().find(AnalysisType::kMax);
    if (mode == point.get_path_state_map().end())
      continue;
    for (auto index : _database.graph().vertices()[*vertex].get_outgoing_arc_list()) {
      auto& arc = _database.graph().arcs()[index];
      if (_query.isDisableArc(arc) || _query.shouldStopDataPropagation(arc))
        continue;
      auto found = frontiers.find(&_database.graph().sinkPoint(arc));
      if (found == frontiers.end())
        continue;
      for (auto input : {TransType::kRise, TransType::kFall}) {
        if (!allowsTransition(_database.network(), _database.graph().simValues(), arc.get_source_pin(), input, arc.get_type() == ArcType::kNet))
          continue;
        for (auto output : _query.getOutputTransTypeList(arc, AnalysisType::kMax, input)) {
          if (!allowsTransition(_database.network(), _database.graph().simValues(), arc.get_sink_pin(), output, arc.get_type() == ArcType::kCell))
            continue;
          const double delay = _query.getDataArcDelay(arc, AnalysisType::kMax, input, output);
          for (const auto& [source, edges] : mode->second) {
            const auto source_edge = edges.find(input);
            const auto sink_source = found->second.find(source);
            if (source_edge == edges.end() || sink_source == found->second.end())
              continue;
            const auto sink_edge = sink_source->second.find(output);
            if (sink_edge == sink_source->second.end())
              continue;
            std::set<TimingStateQuery::PathStateGroup> visited;
            for (const auto& [tag, state] : source_edge->second) {
              const auto group = _query.pathStateGroup(state);
              if (!visited.insert(group).second)
                continue;
              if (state.get_is_clock_source_path() && arc.get_type() == ArcType::kNet
                  && !_database.sdc().clockAllowsNetArc(_database.network(), state.get_clock_name(), arc.get_source_pin(), arc.get_sink_pin()))
                continue;
              const auto progress = matcher.advanceState(_through_index, state.get_exception_state_list(), arc.get_sink_pin(), output,
                                                         arc.get_type() == ArcType::kNet ? arc.get_source_pin() : "");
              const auto candidates = sink_edge->second.find(_query.pathStateGroup(state, &progress));
              if (candidates == sink_edge->second.end())
                continue;
              auto& values = target[source][input][group];
              for (auto candidate : candidates->second) {
                candidate.required -= delay;
                candidate.downstream_delay += delay;
                values.insert(std::move(candidate));
              }
            }
          }
        }
      }
    }
    for (auto& [source, edges] : target)
      for (auto& [edge, groups] : edges)
        for (auto& [group, candidates] : groups)
          candidates.releaseIndexes();
    for (auto& [source, edges] : mode->second)
      for (auto& [edge, states] : edges)
        for (auto& [tag, state] : states) {
          const auto group = target[source][edge].find(_query.pathStateGroup(state));
          if (group == target[source][edge].end())
            continue;
          for (const auto& candidate : group->second) {
            const auto& check = *candidate.check;
            const double cppr = reconvergence.correction(state.get_crpr_clock_pin(), state.get_crpr_clock_trans_type(), state.get_clock_name(),
                                                         check.capture_pin, check.capture_pin_transition, check.capture_clock, AnalysisType::kMax, common,
                                                         state.get_launch_clock_trans_type(), check.capture_transition);
            double required = _query.roundTime(check.required_before_round + cppr);
            if (check.has_required_adjustment)
              required = _query.roundTime(required + check.required_adjustment);
            required -= candidate.downstream_delay;
            const auto previous = _required.find(&state);
            if (previous == _required.end() || required < previous->second) {
              _required[&state] = required;
              if (endpoints.contains(&point))
                _setup_checks[&state] = check.type == TimingCheckType::kSetup ? check.check_time : 0.;
            }
          }
        }
  }
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
