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
#include "ClockArcPropagator.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DelayCalculator.hpp"
#include "TimingTolerance.hpp"
#include "graph/TimingStateQuery.hpp"
#include "network/Network.hpp"

namespace ista {
bool ClockArcPropagator::isDisableArc(Arc& arc)
{
  const auto* cell_arc = arc.libertyArcId() ? _database.liberty().findCellArc(*arc.libertyArcId()) : nullptr;
  return arc.get_is_disable_arc() || arc.get_is_loop_disable() || (cell_arc && cell_arc->isThreeStateArc());
}

void ClockArcPropagator::propagateArrival(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, bool respect_boundaries)
{
  propagateArrival(clock, arc_idx, analysis_type, TransType::kRise, respect_boundaries);
  propagateArrival(clock, arc_idx, analysis_type, TransType::kFall, respect_boundaries);
}

void ClockArcPropagator::propagateArrival(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type,
                                          bool respect_boundaries)
{
  Database& database = _database;
  Arc& arc = database.graph().arcs()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  if ((respect_boundaries && arc.get_type() == ArcType::kNet
       && !database.sdc().clockAllowsNetArc(database.network(), std::string(clock.get_clock_name()), arc.get_source_pin(), arc.get_sink_pin()))
      || (respect_boundaries && (!source_point.has_clock_state(clock.get_clock_name()) || !sink_point.has_clock_state(clock.get_clock_name())))
      || source_point.find_clock_state(clock.get_clock_name()) == nullptr || sink_point.find_clock_state(clock.get_clock_name()) == nullptr) {
    return;
  }
  auto& source_state = source_point.get_clock_state(clock.get_clock_name());
  auto& sink_state = sink_point.get_clock_state(clock.get_clock_name());
  for (const auto& [source_edge, source] : source_state.waveforms) {
    if (!source.physicalArrival(analysis_type, input_trans_type) || !source.physicalSlew(analysis_type, input_trans_type))
      continue;
    for (auto output : getOutputTransTypeList(arc, analysis_type, input_trans_type))
      updateClockPathState(clock.get_clock_name(), source_edge, arc, source, sink_state.waveforms[source_edge], analysis_type, input_trans_type, output);
  }
  sink_state.summarize();
}

void ClockArcPropagator::updateClockPathState(std::string_view clock_name, TransType source_edge, Arc& arc, const ClockWaveform& source, ClockWaveform& sink,
                                              AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  const auto found = _clock_arc_delays.find({std::string(clock_name), &arc, analysis_type, source_edge, input_trans_type, output_trans_type});
  if (found == _clock_arc_delays.end())
    return;
  const double delay = TimingStateQuery(_database).deratedDelay(found->second, analysis_type, true, output_trans_type,
                                                                arc.get_type() == ArcType::kNet ? DerateDelay::kNet : DerateDelay::kCell, arc.get_source_pin());
  const double arrival = roundTime(*source.physicalArrival(analysis_type, input_trans_type) + delay);
  const auto current = sink.physicalArrival(analysis_type, output_trans_type);
  if (!current || isBetterArrival(arrival, *current, analysis_type)) {
    sink.physical_arrival_map[analysis_type][output_trans_type] = arrival;
    sink.predecessor_source_map[analysis_type][output_trans_type] = source_edge;
    sink.physical_predecessor_map[analysis_type][output_trans_type] = arc.get_source_pin();
    sink.predecessor_vertex_map[analysis_type][output_trans_type] = arc.get_source_vertex_idx();
    sink.physical_predecessor_arc_delay_map[analysis_type][output_trans_type] = delay;
    sink.physical_predecessor_trans_type_map[analysis_type][output_trans_type] = input_trans_type;
  }
}

void ClockArcPropagator::updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay)
{
  if (arc.get_graph_delay_map().count(analysis_type) == 0 || arc.get_graph_delay_map()[analysis_type].count(input_trans_type) == 0
      || arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) == 0
      || isBetterDelay(arc_delay, arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type], analysis_type)) {
    arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type] = arc_delay;
  }
}

bool ClockArcPropagator::isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_delay < current_delay - ista::kTimingTolerance;
  }
  return candidate_delay > current_delay + ista::kTimingTolerance;
}

bool ClockArcPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - ista::kTimingTolerance;
  }
  return candidate_slew > current_slew + ista::kTimingTolerance;
}

double ClockArcPropagator::roundTime(double time)
{
  return std::round(time * 1E15) / 1E15;
}

std::vector<TransType> ClockArcPropagator::getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
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

bool ClockArcPropagator::isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type)
{
  if (!isFinite(current_arrival)) {
    return true;
  }
  if (analysis_type == AnalysisType::kMin) {
    return candidate_arrival < current_arrival - ista::kTimingTolerance;
  }
  return candidate_arrival > current_arrival + ista::kTimingTolerance;
}

bool ClockArcPropagator::isFinite(double value)
{
  return std::isfinite(value);
}

void ClockArcPropagator::propagateSlew(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, bool respect_boundaries)
{
  propagateSlew(clock, arc_idx, analysis_type, TransType::kRise, respect_boundaries);
  propagateSlew(clock, arc_idx, analysis_type, TransType::kFall, respect_boundaries);
}

void ClockArcPropagator::propagateSlew(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type, bool respect_boundaries)
{
  Database& database = _database;
  Arc& arc = database.graph().arcs()[arc_idx];
  TimingPoint& source_point = database.graph().sourcePoint(arc);
  TimingPoint& sink_point = database.graph().sinkPoint(arc);
  if ((respect_boundaries && arc.get_type() == ArcType::kNet
       && !database.sdc().clockAllowsNetArc(database.network(), std::string(clock.get_clock_name()), arc.get_source_pin(), arc.get_sink_pin()))
      || (respect_boundaries && (!source_point.has_clock_state(clock.get_clock_name()) || !sink_point.has_clock_state(clock.get_clock_name())))
      || source_point.find_clock_state(clock.get_clock_name()) == nullptr || sink_point.find_clock_state(clock.get_clock_name()) == nullptr) {
    return;
  }
  auto& source_state = source_point.get_clock_state(clock.get_clock_name());
  auto& sink_state = sink_point.get_clock_state(clock.get_clock_name());
  for (const auto& [source_edge, source] : source_state.waveforms) {
    if (!source.physicalSlew(analysis_type, input_trans_type))
      continue;
    for (auto output : getOutputTransTypeList(arc, analysis_type, input_trans_type))
      updateClockSlewDelay(clock.get_clock_name(), source_edge, arc, source, sink_state.waveforms[source_edge], analysis_type, input_trans_type, output);
  }
  sink_state.summarize();
}

void ClockArcPropagator::updateClockSlewDelay(std::string_view clock_name, TransType source_edge, Arc& arc, const ClockWaveform& source, ClockWaveform& sink,
                                              AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  const ArcDelayKey key{std::string(clock_name), &arc, analysis_type, source_edge, input_trans_type, output_trans_type};
  _clock_arc_delays.erase(key);
  const auto result = _delay.arcDelay(arc, analysis_type, input_trans_type, output_trans_type, *source.physicalSlew(analysis_type, input_trans_type));
  if (!result)
    return;
  _clock_arc_delays.emplace(key, result->get_delay());
  updateGraphArcDelay(arc, analysis_type, input_trans_type, output_trans_type, result->get_delay());
  const auto current = sink.physicalSlew(analysis_type, output_trans_type);
  if (!current || isBetterSlew(result->get_slew(), *current, analysis_type))
    sink.physical_slew_map[analysis_type][output_trans_type] = result->get_slew();
}
}  // namespace ista
