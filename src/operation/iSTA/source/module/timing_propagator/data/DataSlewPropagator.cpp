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
#include "DataSlewPropagator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "DelayCalculator.hpp"
#include "TimingTolerance.hpp"
#include "TransitionFilter.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
bool DataSlewPropagator::shouldStopDataSlewPropagation(Arc& arc)
{
  if (arc.get_type() != ArcType::kNet || !_path_query.isSequentialClockPin(arc.get_sink_pin())) {
    return false;
  }
  return _database.graph().sinkPoint(arc).get_is_clock_point();
}

void DataSlewPropagator::propagate()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kDataSlew);
  Database& database = _database;
  for (int32_t vertex_idx : database.graph().vertexOrder()) {
    for (std::size_t arc_idx : database.graph().vertices()[vertex_idx].get_outgoing_arc_list()) {
      if (_path_query.isDisableArc(database.graph().arcs()[arc_idx])) {
        continue;
      }
      propagateDataSlewDelayArc(arc_idx);
    }
  }
}

void DataSlewPropagator::propagateDataSlewDelayArc(std::size_t arc_idx)
{
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kRise);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kFall);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kRise);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kFall);
}

void DataSlewPropagator::propagateDataSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = _database;
  Arc& arc = database.graph().arcs()[arc_idx];
  if (_path_query.isDisableArc(arc)) {
    return;
  }
  if (shouldStopDataSlewPropagation(arc)) {
    return;
  }
  if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_source_pin(), input_trans_type, arc.get_type() == ArcType::kNet)) {
    return;
  }
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  if (!hasDataSlew(source_point, analysis_type, input_trans_type)) {
    return;
  }
  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  for (TransType output_trans_type : _path_query.getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_sink_pin(), output_trans_type, arc.get_type() == ArcType::kCell)) {
      continue;
    }
    updateDataSlewDelay(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void DataSlewPropagator::updateDataSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                                             TransType input_trans_type, TransType output_trans_type)
{
  double input_slew = _path_query.getDataSlew(source_point, analysis_type, input_trans_type);
  std::optional<double> output_slew_input;
  if (_path_query.isSequentialClockPin(arc.get_source_pin())) {
    output_slew_input = _path_query.getClockSlew(arc.get_source_pin(), analysis_type, input_trans_type);
  }
  auto dc_result = _delay.arcDelay(arc, analysis_type, input_trans_type, output_trans_type, input_slew, output_slew_input);
  if (!dc_result.has_value()) {
    return;
  }
  double arc_delay = dc_result->get_delay();
  double output_slew = dc_result->get_slew();
  updateGraphArcDelay(arc, analysis_type, input_trans_type, output_trans_type, arc_delay);
  updateDataSlew(sink_point, analysis_type, output_trans_type, output_slew);
}

void DataSlewPropagator::updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay)
{
  if (arc.get_graph_delay_map().count(analysis_type) == 0 || arc.get_graph_delay_map()[analysis_type].count(input_trans_type) == 0
      || arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) == 0
      || isBetterDelay(arc_delay, arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type], analysis_type)) {
    arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type] = arc_delay;
  }
}

void DataSlewPropagator::updateDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double data_slew)
{
  if (!hasDataSlew(timing_point, analysis_type, trans_type)
      || isBetterSlew(data_slew, timing_point.get_data_slew_map()[analysis_type][trans_type], analysis_type)) {
    timing_point.get_data_slew_map()[analysis_type][trans_type] = data_slew;
  }
}

bool DataSlewPropagator::hasDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  return timing_point.get_data_slew_map().count(analysis_type) > 0 && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0;
}

bool DataSlewPropagator::isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_delay < current_delay - ista::kTimingTolerance;
  }
  return candidate_delay > current_delay + ista::kTimingTolerance;
}

bool DataSlewPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - ista::kTimingTolerance;
  }
  return candidate_slew > current_slew + ista::kTimingTolerance;
}
}  // namespace ista
