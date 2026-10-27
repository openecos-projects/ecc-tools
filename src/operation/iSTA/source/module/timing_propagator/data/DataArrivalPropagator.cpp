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
#include "DataArrivalPropagator.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "TransitionFilter.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
void DataArrivalPropagator::propagate()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kDataArrival);
  Database& database = _database;
  _through_index.rebuild(database.sdc(), database.network());
  for (int32_t vertex_idx : database.graph().vertexOrder()) {
    for (std::size_t arc_idx : database.graph().vertices()[vertex_idx].get_outgoing_arc_list()) {
      if (_path_query.isDisableArc(database.graph().arcs()[arc_idx])) {
        continue;
      }
      if (_path_query.shouldStopDataPropagation(database.graph().arcs()[arc_idx])) {
        continue;
      }
      propagateArrivalArc(arc_idx);
    }
  }
}

void DataArrivalPropagator::propagateArrivalArc(std::size_t arc_idx)
{
  Database& database = _database;
  const double kEpsilon = 1e-9;
  Arc& arc = database.graph().arcs()[arc_idx];
  if (_path_query.isDisableArc(arc)) {
    return;
  }
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  if (_path_query.isFinite(source_point.get_arrival())) {
    const double candidate_arrival = source_point.get_arrival() + _path_query.maxDataArcDelay(arc);
    if (_path_query.isFinite(candidate_arrival)
        && (!_path_query.isFinite(sink_point.get_arrival()) || candidate_arrival > sink_point.get_arrival() + kEpsilon)) {
      sink_point.set_arrival(candidate_arrival);
      sink_point.set_predecessor(arc.get_source_pin());
      sink_point.set_predecessor_arc_idx(arc_idx);
      sink_point.set_launch_time(source_point.get_launch_time());
      sink_point.setLaunchClock(source_point.launchClock());
    }
  }
  propagatePathStateArc(arc_idx, AnalysisType::kMax, PathSourceType::kInput);
  propagatePathStateArc(arc_idx, AnalysisType::kMax, PathSourceType::kRegister);
  propagatePathStateArc(arc_idx, AnalysisType::kMin, PathSourceType::kInput);
  propagatePathStateArc(arc_idx, AnalysisType::kMin, PathSourceType::kRegister);
}

void DataArrivalPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type)
{
  propagatePathStateArc(arc_idx, analysis_type, source_type, TransType::kRise);
  propagatePathStateArc(arc_idx, analysis_type, source_type, TransType::kFall);
}

void DataArrivalPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type)
{
  Database& database = _database;
  if (_path_query.isDisableArc(database.graph().arcs()[arc_idx])) {
    return;
  }
  Arc& arc = database.graph().arcs()[arc_idx];
  if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_source_pin(), input_trans_type, arc.get_type() == ArcType::kNet)) {
    return;
  }
  for (TransType output_trans_type : _path_query.getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_sink_pin(), output_trans_type, arc.get_type() == ArcType::kCell)) {
      continue;
    }
    propagatePathStateArc(arc_idx, analysis_type, source_type, input_trans_type, output_trans_type);
  }
}

void DataArrivalPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type,
                                                  TransType output_trans_type)
{
  Database& database = _database;
  Arc& arc = database.graph().arcs()[arc_idx];
  if (_path_query.isDisableArc(arc)) {
    return;
  }
  if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_source_pin(), input_trans_type, arc.get_type() == ArcType::kNet)
      || !allowsTransition(database.network(), database.graph().simValues(), arc.get_sink_pin(), output_trans_type, arc.get_type() == ArcType::kCell)) {
    return;
  }
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  if (!_path_query.hasPathState(source_point, analysis_type, source_type, input_trans_type)) {
    return;
  }

  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  std::map<std::string, TimingPathState>& source_path_state_map = _path_query.getPathStateMap(source_point, analysis_type, source_type, input_trans_type);
  for (std::pair<const std::string, TimingPathState>& source_path_state_pair : source_path_state_map) {
    TimingPathState& source_path_state = source_path_state_pair.second;
    if (!_path_query.isFinite(source_path_state.get_arrival())) {
      continue;
    }
    if (source_path_state.get_is_clock_source_path() && arc.get_type() == ArcType::kNet
        && !database.sdc().clockAllowsNetArc(database.network(), source_path_state.get_clock_name(), arc.get_source_pin(), arc.get_sink_pin()))
      continue;
    double arc_delay = _path_query.getDataArcDelay(arc, analysis_type, input_trans_type, output_trans_type);
    double candidate_arrival = _path_query.roundTime(source_path_state.get_arrival() + arc_delay);
    const std::vector<int32_t> exception_state_list = ExceptionMatcher(database.sdc(), database.network())
                                                          .advanceState(_through_index, source_path_state.get_exception_state_list(), arc.get_sink_pin(),
                                                                        output_trans_type, arc.get_type() == ArcType::kNet ? arc.get_source_pin() : "");
    const std::string path_state_tag
        = ExceptionMatcher(database.sdc(), database.network())
              .makePathStateTag(source_path_state.get_clock_name(), source_path_state.get_launch_clock_trans_type(), exception_state_list,
                                source_path_state.get_is_clock_source_path(), source_path_state.crprPathTag(), source_path_state.get_launch_clock_edge_index());
    std::map<std::string, TimingPathState>& sink_path_state_map = _path_query.getPathStateMap(sink_point, analysis_type, source_type, output_trans_type);
    if (sink_path_state_map.count(path_state_tag) == 0
        || _path_query.isBetterArrival(candidate_arrival, sink_path_state_map[path_state_tag].get_arrival(), analysis_type)) {
      TimingPathState& sink_path_state = sink_path_state_map[path_state_tag];
      sink_path_state = source_path_state;
      sink_path_state.set_arrival(candidate_arrival);
      sink_path_state.set_slew(_path_query.getDataSlew(sink_point, analysis_type, output_trans_type));
      sink_path_state.set_start_point(source_path_state.get_start_point());
      sink_path_state.set_predecessor(arc.get_source_pin());
      sink_path_state.set_predecessor_arc_idx(arc_idx);
      sink_path_state.set_predecessor_arc_delay(arc_delay);
      sink_path_state.set_launch_time(source_path_state.get_launch_time());
      sink_path_state.set_clock_name(source_path_state.get_clock_name());
      sink_path_state.set_is_clock_source_path(source_path_state.get_is_clock_source_path());
      sink_path_state.set_crpr_clock_pin(source_path_state.get_crpr_clock_pin());
      sink_path_state.set_path_state_tag(path_state_tag);
      sink_path_state.set_predecessor_path_state_tag(source_path_state_pair.first);
      sink_path_state.set_exception_state_list(exception_state_list);
      sink_path_state.set_trans_type(output_trans_type);
      sink_path_state.set_predecessor_trans_type(input_trans_type);
      sink_path_state.set_crpr_clock_trans_type(source_path_state.get_crpr_clock_trans_type());
    }
  }
}
}  // namespace ista
