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
#include "PathSearcher.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "TimingTolerance.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {

std::vector<TimingPath> PathSearcher::findPathEnds(const std::string& end_point)
{
  std::vector<TimingPath> timing_path_list;
  if (!_query.isConstrainedEndPoint(end_point)) {
    return timing_path_list;
  }
  // Complete DAG propagation already retained every non-dominated launch.
  // Diversion repair must not reinsert candidates removed by proven bounds.
  if (!_database.graph().pathStatesPruned())
    buildPathDiversionList(end_point);
  for (AnalysisType analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
    for (PathSourceType source : {PathSourceType::kInput, PathSourceType::kRegister}) {
      if (auto path_end = findWorstPathEnd(end_point, analysis, source)) {
        timing_path_list.push_back(_trace.buildTimingPath(end_point, analysis, source, *path_end));
      }
    }
  }
  return timing_path_list;
}

void PathSearcher::buildPathDiversionList(const std::string& end_point)
{
  buildPathDiversionList(end_point, AnalysisType::kMax, PathSourceType::kInput);
  buildPathDiversionList(end_point, AnalysisType::kMax, PathSourceType::kRegister);
  buildPathDiversionList(end_point, AnalysisType::kMin, PathSourceType::kInput);
  buildPathDiversionList(end_point, AnalysisType::kMin, PathSourceType::kRegister);
}

void PathSearcher::buildPathDiversionList(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type)
{
  Database& database = _database;
  TimingPoint& end_timing_point = database.graph().points()[end_point];
  TimingPathState* path_state = _query.getWorstPathState(end_timing_point, analysis_type, source_type);
  if (path_state == nullptr) {
    return;
  }
  std::vector<std::string> path_pin_name_list;
  std::vector<TransType> path_trans_type_list;
  std::vector<std::string> path_state_tag_list;
  std::vector<std::size_t> path_arc_idx_list;
  _trace.buildPathTrace(end_point, analysis_type, source_type, *path_state, path_pin_name_list, path_trans_type_list, path_state_tag_list, path_arc_idx_list);
  buildPathDiversionList(end_point, analysis_type, source_type, path_pin_name_list, path_trans_type_list, path_arc_idx_list);
}

void PathSearcher::buildPathDiversionList(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type,
                                          std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list,
                                          std::vector<std::size_t>& path_arc_idx_list)
{
  Database& database = _database;
  for (std::size_t sink_idx = 1; sink_idx < path_pin_name_list.size(); sink_idx++) {
    TransType sink_trans_type = path_trans_type_list[sink_idx];
    std::size_t path_arc_idx = path_arc_idx_list[sink_idx - 1];
    for (std::size_t diversion_arc_idx : database.graph().vertices()[database.graph().arcs()[path_arc_idx].get_sink_vertex_idx()].get_incoming_arc_list()) {
      if (diversion_arc_idx == path_arc_idx || _query.isDisableArc(database.graph().arcs()[diversion_arc_idx])
          || _query.shouldStopDataPropagation(database.graph().arcs()[diversion_arc_idx])) {
        continue;
      }
      Arc& diversion_arc = database.graph().arcs()[diversion_arc_idx];
      TimingPoint& source_point = database.graph().sourcePoint(diversion_arc);
      for (TransType input_trans_type : {TransType::kRise, TransType::kFall}) {
        if (!_query.hasPathState(source_point, analysis_type, source_type, input_trans_type)
            || !isOutputTransType(diversion_arc, analysis_type, input_trans_type, sink_trans_type)) {
          continue;
        }
        std::map<std::string, TimingPathState>& source_path_state_map = _query.getPathStateMap(source_point, analysis_type, source_type, input_trans_type);
        for (std::pair<const std::string, TimingPathState>& source_path_state_pair : source_path_state_map) {
          const TimingPathState& source_path_state = source_path_state_pair.second;
          if (!_query.isFinite(source_path_state.get_arrival())) {
            continue;
          }
          buildPathDiversionState(analysis_type, source_type, path_pin_name_list, path_trans_type_list, path_arc_idx_list, sink_idx, diversion_arc_idx,
                                  input_trans_type, source_path_state);
        }
      }
    }
  }
}

void PathSearcher::buildPathDiversionState(AnalysisType analysis_type, PathSourceType source_type, std::vector<std::string>& path_pin_name_list,
                                           std::vector<TransType>& path_trans_type_list, std::vector<std::size_t>& path_arc_idx_list, std::size_t sink_idx,
                                           std::size_t diversion_arc_idx, TransType input_trans_type, const TimingPathState& source_path_state)
{
  Database& database = _database;
  Arc& diversion_arc = database.graph().arcs()[diversion_arc_idx];
  TransType sink_trans_type = path_trans_type_list[sink_idx];
  double diversion_arc_delay = _query.getDataArcDelay(diversion_arc, analysis_type, input_trans_type, sink_trans_type);
  double arrival = _query.roundTime(source_path_state.get_arrival() + diversion_arc_delay);
  std::string predecessor = diversion_arc.get_source_pin();
  TimingPathState* current_path_state = updateDiversionPathState(path_pin_name_list[sink_idx], analysis_type, source_type, sink_trans_type, source_path_state,
                                                                 predecessor, diversion_arc_idx, diversion_arc_delay, input_trans_type, arrival);
  if (current_path_state == nullptr) {
    return;
  }

  for (std::size_t path_idx = sink_idx + 1; path_idx < path_pin_name_list.size(); path_idx++) {
    Arc& path_arc = database.graph().arcs()[path_arc_idx_list[path_idx - 1]];
    TransType predecessor_trans_type = path_trans_type_list[path_idx - 1];
    TransType trans_type = path_trans_type_list[path_idx];
    double arc_delay = _query.getDataArcDelay(path_arc, analysis_type, predecessor_trans_type, trans_type);
    arrival = _query.roundTime(arrival + arc_delay);
    std::string& path_predecessor = path_pin_name_list[path_idx - 1];
    current_path_state = updateDiversionPathState(path_pin_name_list[path_idx], analysis_type, source_type, trans_type, *current_path_state, path_predecessor,
                                                  path_arc_idx_list[path_idx - 1], arc_delay, predecessor_trans_type, arrival);
    if (current_path_state == nullptr) {
      return;
    }
  }
}

bool PathSearcher::isOutputTransType(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  for (TransType arc_output_trans_type : _query.getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    if (arc_output_trans_type == output_trans_type) {
      return true;
    }
  }
  return false;
}

TimingPathState* PathSearcher::updateDiversionPathState(const std::string& pin_name, AnalysisType analysis_type, PathSourceType source_type,
                                                        TransType trans_type, const TimingPathState& source_path_state, const std::string& predecessor,
                                                        std::size_t predecessor_arc_idx, double predecessor_arc_delay, TransType predecessor_trans_type,
                                                        double arrival)
{
  Database& database = _database;
  const auto& incoming = database.graph().arcs().at(predecessor_arc_idx);
  if (source_path_state.get_is_clock_source_path() && incoming.get_type() == ArcType::kNet
      && !database.sdc().clockAllowsNetArc(database.network(), source_path_state.get_clock_name(), predecessor, pin_name))
    return nullptr;
  TimingPoint& timing_point = database.graph().sinkPoint(database.graph().arcs()[predecessor_arc_idx]);
  const std::vector<int32_t> exception_state_list
      = ExceptionMatcher(database.sdc(), database.network())
            .advanceState(_through_index, source_path_state.get_exception_state_list(), pin_name, trans_type,
                          database.graph().arcs()[predecessor_arc_idx].get_type() == ArcType::kNet ? predecessor : "");
  const std::string path_state_tag
      = ExceptionMatcher(database.sdc(), database.network())
            .makePathStateTag(source_path_state.get_clock_name(), source_path_state.get_launch_clock_trans_type(), exception_state_list,
                              source_path_state.get_is_clock_source_path(), source_path_state.crprPathTag(), source_path_state.get_launch_clock_edge_index());
  std::map<std::string, TimingPathState>& path_state_map = _query.getPathStateMap(timing_point, analysis_type, source_type, trans_type);
  if (path_state_map.count(path_state_tag) > 0 && !_query.isBetterArrival(arrival, path_state_map[path_state_tag].get_arrival(), analysis_type)) {
    return nullptr;
  }
  TimingPathState& path_state = path_state_map[path_state_tag];
  path_state = source_path_state;
  path_state.set_arrival(arrival);
  path_state.set_slew(_query.getDataSlew(timing_point, analysis_type, trans_type));
  path_state.set_start_point(source_path_state.get_start_point());
  path_state.set_predecessor(predecessor);
  path_state.set_predecessor_arc_idx(predecessor_arc_idx);
  path_state.set_predecessor_arc_delay(predecessor_arc_delay);
  path_state.set_launch_time(source_path_state.get_launch_time());
  path_state.set_clock_name(source_path_state.get_clock_name());
  path_state.set_is_clock_source_path(source_path_state.get_is_clock_source_path());
  path_state.set_crpr_clock_pin(source_path_state.get_crpr_clock_pin());
  path_state.set_path_state_tag(path_state_tag);
  path_state.set_predecessor_path_state_tag(source_path_state.get_path_state_tag());
  path_state.set_exception_state_list(exception_state_list);
  path_state.set_trans_type(trans_type);
  path_state.set_predecessor_trans_type(predecessor_trans_type);
  path_state.set_crpr_clock_trans_type(source_path_state.get_crpr_clock_trans_type());
  return &path_state;
}

std::optional<PathEnd> PathSearcher::findWorstPathEnd(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type)
{
  Database& database = _database;
  TimingPoint& timing_point = database.graph().points()[end_point];
  std::optional<PathEnd> worst;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    if (!_query.hasPathState(timing_point, analysis_type, source_type, trans_type))
      continue;
    const auto& states = _query.getPathStateMap(timing_point, analysis_type, source_type, trans_type);
    for (const auto& [tag, path] : states) {
      if (!_query.isFinite(path.get_arrival()))
        continue;
      auto path_worst = _check.checkPath(path, end_point, analysis_type);
      // Keep the two-stage reduction: ista::kTimingTolerance comparisons are not transitive.
      if (path_worst && (!worst || path_worst->slack() < worst->slack() - ista::kTimingTolerance))
        worst = std::move(path_worst);
    }
  }
  return worst;
}

}  // namespace ista
