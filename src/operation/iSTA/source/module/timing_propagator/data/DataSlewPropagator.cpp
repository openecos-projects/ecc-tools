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

#include <cstdio>
#include <cstdlib>

#include <cstdlib>
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

bool DataSlewPropagator::constant_driven_arc_enabled()
{
  static const bool enabled = [] {
    const char* env = std::getenv("ISTA_CONST_CELL_SLEW");
    return env == nullptr || std::atoi(env) != 0;
  }();
  return enabled;
}

// reference keeps the arcs of a fanout cone that is driven by a logic constant:
// the liberty delay/transition tables are read at input_slew = 0 and the result
// reaches SDF.  iSTA instead marks those arcs is_disable_arc, so they are
// dropped from slew propagation and from the SDF net list.  Measured on i2c:
// 2 cell + 4 net arcs disabled, exactly the arcs of the two TIELO-driven
// fixio_buf instances, and REF emits INTERCONNECT/IOPATH for both of them.
// A loop-cut arc is never re-enabled here; only the constant cone is.
bool DataSlewPropagator::is_constant_driven_arc(Arc& arc)
{
  if (!constant_driven_arc_enabled() || arc.get_is_loop_disable() || !arc.get_is_disable_arc()) {
    return false;
  }
  const auto& values = _database.graph().simValues();
  const bool driver_side = arc.get_type() == ArcType::kNet;
  const auto source_value = values.value(arc.get_source_pin(), driver_side);
  if (!source_value.has_value()) {
    return false;
  }
  return *source_value == TimingCaseValue::kZero || *source_value == TimingCaseValue::kOne
         || *source_value == TimingCaseValue::kStatic;
}

void DataSlewPropagator::propagate()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kDataSlew);
  Database& database = _database;
  for (int32_t vertex_idx : database.graph().vertexOrder()) {
    for (std::size_t arc_idx : database.graph().vertices()[vertex_idx].get_outgoing_arc_list()) {
      if (_path_query.isDisableArc(database.graph().arcs()[arc_idx])
          && !is_constant_driven_arc(database.graph().arcs()[arc_idx])) {
        continue;
      }
      propagateDataSlewDelayArc(arc_idx);
    }
  }
  // A constant-driven arc is disabled, so its sink vertex never receives an
  // indegree from it and the vertex order can place that sink before its own
  // driver.  Re-sweeping the constant cone picks up the driver slew produced by
  // the first sweep; without it a port behind a tied buffer keeps slew 0 while
  // reference reports the driver slew unchanged.
  if (!constant_driven_arc_enabled()) {
    return;
  }
  std::vector<std::size_t> disabled_arc_list;
  for (int32_t vertex_idx : database.graph().vertexOrder()) {
    for (std::size_t arc_idx : database.graph().vertices()[vertex_idx].get_outgoing_arc_list()) {
      Arc& arc = database.graph().arcs()[arc_idx];
      if (arc.get_is_disable_arc() && !arc.get_is_loop_disable()) {
        disabled_arc_list.push_back(arc_idx);
      }
    }
  }
  if (disabled_arc_list.empty()) {
    return;
  }
  for (int sweep = 0; sweep < 3; ++sweep) {
    for (std::size_t arc_idx : disabled_arc_list) {
      propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kRise, true);
      propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kFall, true);
      propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kRise, true);
      propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kFall, true);
    }
  }
}

void DataSlewPropagator::propagateDataSlewDelayArc(std::size_t arc_idx)
{
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kRise, false);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kFall, false);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kRise, false);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kFall, false);
}

void DataSlewPropagator::propagateDataSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type, bool force)
{
  Database& database = _database;
  Arc& arc = database.graph().arcs()[arc_idx];
  const bool constant_driven = is_constant_driven_arc(arc);
  if (_path_query.isDisableArc(arc) && !constant_driven && !force) {
    return;
  }
  if (shouldStopDataSlewPropagation(arc)) {
    return;
  }
  const bool source_gated = constant_driven || force;
  if (!source_gated
      && !allowsTransition(database.network(), database.graph().simValues(), arc.get_source_pin(), input_trans_type, arc.get_type() == ArcType::kNet)) {
    return;
  }
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  // A constant-driven net has no driver slew; a constant-driven cell arc is still
  // evaluated, because REF reads the liberty table at input_slew = 0 there.
  if (!hasDataSlew(source_point, analysis_type, input_trans_type) && !source_gated) {
    return;
  }
  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  for (TransType output_trans_type : _path_query.getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    if (!source_gated
        && !allowsTransition(database.network(), database.graph().simValues(), arc.get_sink_pin(), output_trans_type, arc.get_type() == ArcType::kCell)) {
      continue;
    }
    updateDataSlewDelay(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type, force);
  }
}

void DataSlewPropagator::updateDataSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                                             TransType input_trans_type, TransType output_trans_type, bool force)
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
  updateGraphArcDelay(arc, analysis_type, input_trans_type, output_trans_type, arc_delay, force);
  updateDataSlew(sink_point, analysis_type, output_trans_type, output_slew, force);
}

void DataSlewPropagator::updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay,
                                            bool force)
{
  if (force && std::isfinite(arc_delay)) {
    arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type] = arc_delay;
    return;
  }
  if (arc.get_graph_delay_map().count(analysis_type) == 0 || arc.get_graph_delay_map()[analysis_type].count(input_trans_type) == 0
      || arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) == 0
      || isBetterDelay(arc_delay, arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type], analysis_type)) {
    arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type] = arc_delay;
  }
}

void DataSlewPropagator::updateDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double data_slew, bool force)
{
  // A constant cone is resolved in a follow-up sweep, after the tied driver has
  // picked up its own slew.  The min-slew rule would keep the placeholder zero
  // written by that first sweep, so the re-sweep overwrites it.
  if (force && std::isfinite(data_slew)) {
    timing_point.get_data_slew_map()[analysis_type][trans_type] = data_slew;
    return;
  }
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
