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
#include "PathTracer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ista {

TimingPath PathTracer::buildTimingPath(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type, const PathEnd& path_end)
{
  Database& database = _database;
  const TimingPathState& end_path_state = path_end.path();
  const std::string& start_point = end_path_state.get_start_point();
  const TransType trans_type = end_path_state.get_trans_type();
  std::vector<std::string> path_pin_name_list;
  std::vector<TransType> path_trans_type_list;
  std::vector<std::string> path_state_tag_list;
  std::vector<std::size_t> path_arc_idx_list;
  buildPathTrace(end_point, analysis_type, source_type, end_path_state, path_pin_name_list, path_trans_type_list, path_state_tag_list, path_arc_idx_list);
  const double required_time = path_end.required();
  const double slack = path_end.slack();
  TimingPath timing_path;
  timing_path.set_start_point(start_point);
  timing_path.set_end_point(end_point);
  timing_path.set_path_delay(end_path_state.get_arrival());
  timing_path.set_required_time(required_time);
  timing_path.set_required_time_adjustment(_query.roundTime(required_time - path_end.normalRequired()));
  timing_path.set_slack(slack);
  timing_path.set_level(database.graph().points()[end_point].get_level());
  timing_path.set_analysis_type(analysis_type);
  timing_path.set_source_type(source_type);
  timing_path.set_trans_type(trans_type);
  const auto& check = path_end.check();
  timing_path.set_check_type(check.type);
  timing_path.set_check_time(check.check_time);
  timing_path.set_clock_uncertainty(check.uncertainty);
  timing_path.set_output_delay(check.output_delay);
  timing_path.set_input_delay(end_path_state.get_input_delay());
  updateClockInfo(timing_path, end_path_state, check);

  Arc* arc = nullptr;
  for (size_t i = 0; i < path_pin_name_list.size(); i++) {
    double arc_delay = 0.0;
    if (i > 0) {
      arc = &database.graph().arcs()[path_arc_idx_list[i - 1]];
      TimingPathState& path_state
          = _query.getPathState(database.graph().sinkPoint(*arc), analysis_type, source_type, path_trans_type_list[i], path_state_tag_list[i]);
      arc_delay = path_state.get_predecessor_arc_delay();
      updatePathDelay(timing_path, arc, arc_delay);
    }
    timing_path.get_point_list().push_back(makeTimingPathPoint(path_pin_name_list[i], arc, analysis_type, source_type,
                                                               i > 0 ? path_trans_type_list[i - 1] : TransType::kNone, path_trans_type_list[i],
                                                               path_state_tag_list[i]));
    if (i > 0) {
      timing_path.get_point_list().back().set_arc_delay(arc_delay);
    }
  }
  return timing_path;
}

void PathTracer::buildPathTrace(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type, const TimingPathState& end_path_state,
                                std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list,
                                std::vector<std::string>& path_state_tag_list, std::vector<std::size_t>& path_arc_idx_list)
{
  Database& database = _database;
  std::string pin_name = end_point;
  const TimingPathState* state = &end_path_state;
  while (!pin_name.empty()) {
    path_pin_name_list.push_back(pin_name);
    path_trans_type_list.push_back(state->get_trans_type());
    path_state_tag_list.push_back(state->get_path_state_tag());
    if (state->get_predecessor().empty()) {
      break;
    }
    std::size_t arc_idx = state->get_predecessor_arc_idx();
    Arc& arc = database.graph().arcs()[arc_idx];
    path_arc_idx_list.push_back(arc_idx);
    pin_name = arc.get_source_pin();
    state = &_query.getPathState(database.graph().sourcePoint(arc), analysis_type, source_type, state->get_predecessor_trans_type(),
                                 state->get_predecessor_path_state_tag());
  }
  std::reverse(path_pin_name_list.begin(), path_pin_name_list.end());
  std::reverse(path_trans_type_list.begin(), path_trans_type_list.end());
  std::reverse(path_state_tag_list.begin(), path_state_tag_list.end());
  std::reverse(path_arc_idx_list.begin(), path_arc_idx_list.end());
}

void PathTracer::updatePathDelay(TimingPath& timing_path, Arc* arc, double arc_delay)
{
  if (arc == nullptr) {
    return;
  }
  if (arc->get_type() == ArcType::kCell) {
    timing_path.set_cell_delay(timing_path.get_cell_delay() + arc_delay);
  } else if (arc->get_type() == ArcType::kNet) {
    timing_path.set_net_delay(timing_path.get_net_delay() + arc_delay);
  }
}

void PathTracer::updateClockInfo(TimingPath& timing_path, const TimingPathState& state, const PathCheck& check)
{
  timing_path.set_launch_time(state.get_launch_time());
  timing_path.set_clock_name(state.get_clock_name());
  TransType launch_transition = state.get_launch_clock_trans_type();
  // Propagation records the selected launch edge, including clock_fall IO.
  timing_path.set_launch_clock_transition(launch_transition);
  timing_path.set_launch_clock_pin_transition(state.get_crpr_clock_trans_type());
  timing_path.set_launch_clock_pin(state.get_crpr_clock_pin());
  const auto clock = _database.sdc().get_clock_map().find(state.get_clock_name());
  const double edge = clock == _database.sdc().get_clock_map().end()
                          ? 0.0
                          : clock->second.edge(state.get_launch_clock_edge_index().value_or(launch_transition == TransType::kFall ? 1 : 0)).time;
  timing_path.set_launch_clock_source_latency(state.get_launch_source_latency());
  timing_path.set_launch_clock_network_delay(state.get_launch_time() - edge - state.get_launch_source_latency());
  timing_path.set_capture_clock_name(check.capture_clock);
  timing_path.set_capture_clock_transition(check.capture_transition);
  timing_path.set_capture_clock_pin_transition(check.capture_pin_transition);
  timing_path.set_capture_clock_pin(check.capture_pin);
  timing_path.set_capture_time(check.capture_time);
  timing_path.set_capture_clock_source_latency(check.source_latency);
  timing_path.set_capture_clock_network_delay(check.network_delay);
  timing_path.set_clock_reconvergence_pessimism(check.reconvergence);
  timing_path.set_last_common_pin(check.common_pin);
  timing_path.set_setup_time(check.check_time);
}

TimingPathPoint PathTracer::makeTimingPathPoint(const std::string& pin_name, Arc* arc, AnalysisType analysis_type, PathSourceType source_type,
                                                TransType input_trans_type, TransType trans_type, const std::string& path_state_tag)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[pin_name];
  TimingPoint& timing_point = arc == nullptr ? database.graph().driverPoint(pin_name) : database.graph().sinkPoint(*arc);
  TimingPathState& path_state = _query.getPathState(timing_point, analysis_type, source_type, trans_type, path_state_tag);
  TimingPathPoint path_point;
  path_point.set_pin_name(pin_name);
  path_point.set_instance_name(pin.get_instance_name());
  if (!pin.get_instance_name().empty()) {
    path_point.set_cell_name(database.network().instances()[pin.get_instance_name()].get_cell_name());
  }
  path_point.set_net_name(pin.get_net_name());
  path_point.set_arrival(path_state.get_arrival());
  path_point.set_required(timing_point.get_required());
  path_point.set_slack(timing_point.get_slack());
  path_point.set_trans_type(trans_type);
  if (arc != nullptr) {
    path_point.set_arc_name(arc->get_arc_name());
    path_point.set_source_pin(arc->get_source_pin());
    path_point.set_sink_pin(arc->get_sink_pin());
    path_point.set_arc_type(arc->get_type());
    path_point.set_arc_delay(path_state.get_predecessor_arc_delay());
    path_point.setRcFailed(arc->rcFailed(analysis_type, trans_type));
    path_point.setRcNonbounding(arc->rcNonbounding());
  }
  return path_point;
}

}  // namespace ista
