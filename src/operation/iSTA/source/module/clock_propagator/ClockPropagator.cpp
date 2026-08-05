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

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ista {

// public

void ClockPropagator::initInst()
{
  if (_cp_instance == nullptr) {
    _cp_instance = new ClockPropagator();
  }
}

ClockPropagator& ClockPropagator::getInst()
{
  if (_cp_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_cp_instance;
}

void ClockPropagator::destroyInst()
{
  if (_cp_instance != nullptr) {
    delete _cp_instance;
    _cp_instance = nullptr;
  }
}

// function

void ClockPropagator::propagate()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  initTimingPointList();
  CPModel cp_model = initCPModel();
  buildClockSourceList(cp_model);
  markClockPointList(cp_model);
  propagateClockArrival(cp_model);
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

ClockPropagator* ClockPropagator::_cp_instance = nullptr;


bool ClockPropagator::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

CPModel ClockPropagator::initCPModel()
{
  CPModel cp_model;
  return cp_model;
}

void ClockPropagator::buildClockSourceList(CPModel& cp_model)
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    for (std::string& clock_source : clock_pair.second.get_source_list()) {
      cp_model.get_clock_source_list().push_back(clock_source);
    }
  }
}

void ClockPropagator::initTimingPointList()
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, TimingPoint>& timing_pair : database.get_timing_point_map()) {
    timing_pair.second.set_arrival(-std::numeric_limits<double>::infinity());
    timing_pair.second.set_required(std::numeric_limits<double>::infinity());
    timing_pair.second.set_slack(0.0);
    timing_pair.second.set_launch_time(0.0);
    timing_pair.second.get_predecessor().clear();
    timing_pair.second.get_clock_name().clear();
    timing_pair.second.get_clock_arrival_map().clear();
    timing_pair.second.get_path_state_map().clear();
    timing_pair.second.get_data_slew_map().clear();
    timing_pair.second.get_clock_predecessor_map().clear();
    timing_pair.second.get_clock_predecessor_arc_delay_map().clear();
    timing_pair.second.get_clock_predecessor_trans_type_map().clear();
    timing_pair.second.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
    timing_pair.second.set_is_clock_point(false);
  }
}

void ClockPropagator::markClockPointList(CPModel& cp_model)
{
  for (std::string& clock_source : cp_model.get_clock_source_list()) {
    markClockPoint(clock_source);
  }
}

void ClockPropagator::markClockPoint(std::string& clock_source)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(clock_source) == 0) {
    return;
  }

  std::queue<std::string> pin_queue;
  database.get_timing_point_map()[clock_source].set_is_clock_point(true);
  pin_queue.push(clock_source);

  while (!pin_queue.empty()) {
    std::string pin_name = pin_queue.front();
    pin_queue.pop();

    if (shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc)) {
        continue;
      }
      TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
      if (sink_point.get_is_clock_point()) {
        continue;
      }
      sink_point.set_is_clock_point(true);
      pin_queue.push(arc.get_sink_pin());
    }
  }
}

void ClockPropagator::propagateClockArrival(CPModel& cp_model)
{
  Database& database = STADM.getDatabase();
  for (std::string& clock_source : cp_model.get_clock_source_list()) {
    seedClockArrival(clock_source);
  }
  propagateClockSlewDelay();

  for (std::string& pin_name : database.get_timing_order_list()) {
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    if (!timing_point.get_is_clock_point()) {
      continue;
    }
    if (shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (isDisableArc(database.get_arc_list()[arc_idx])) {
        continue;
      }
      propagateClockArrivalArc(arc_idx, AnalysisType::kMax);
      propagateClockArrivalArc(arc_idx, AnalysisType::kMin);
    }
  }
}

void ClockPropagator::seedClockArrival(std::string& clock_source)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(clock_source) == 0) {
    return;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[clock_source];
  timing_point.get_clock_arrival_map()[AnalysisType::kMax][TransType::kRise] = 0.0;
  timing_point.get_clock_arrival_map()[AnalysisType::kMax][TransType::kFall] = 0.0;
  timing_point.get_clock_arrival_map()[AnalysisType::kMin][TransType::kRise] = 0.0;
  timing_point.get_clock_arrival_map()[AnalysisType::kMin][TransType::kFall] = 0.0;
  timing_point.get_clock_slew_map()[AnalysisType::kMax][TransType::kRise] = 0.0;
  timing_point.get_clock_slew_map()[AnalysisType::kMax][TransType::kFall] = 0.0;
  timing_point.get_clock_slew_map()[AnalysisType::kMin][TransType::kRise] = 0.0;
  timing_point.get_clock_slew_map()[AnalysisType::kMin][TransType::kFall] = 0.0;
}

void ClockPropagator::propagateClockSlewDelay()
{
  Database& database = STADM.getDatabase();
  for (std::string& pin_name : database.get_timing_order_list()) {
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    if (!timing_point.get_is_clock_point() || shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (isDisableArc(database.get_arc_list()[arc_idx])) {
        continue;
      }
      propagateClockSlewDelayArc(arc_idx, AnalysisType::kMax);
      propagateClockSlewDelayArc(arc_idx, AnalysisType::kMin);
    }
  }
}

void ClockPropagator::propagateClockSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type)
{
  propagateClockSlewDelayArc(arc_idx, analysis_type, TransType::kRise);
  propagateClockSlewDelayArc(arc_idx, analysis_type, TransType::kFall);
}

void ClockPropagator::propagateClockSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = STADM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  if (!source_point.get_is_clock_point() || !sink_point.get_is_clock_point()) {
    return;
  }
  if (source_point.get_clock_slew_map().count(analysis_type) == 0
      || source_point.get_clock_slew_map()[analysis_type].count(input_trans_type) == 0) {
    return;
  }
  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    updateClockSlewDelay(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void ClockPropagator::updateClockSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                                            TransType input_trans_type, TransType output_trans_type)
{
  double input_slew = source_point.get_clock_slew_map()[analysis_type][input_trans_type];
  DCTask dc_task;
  dc_task.set_proc_type(DCProcType::kCalculate);
  dc_task.set_arc(&arc);
  dc_task.set_analysis_type(analysis_type);
  dc_task.set_input_trans_type(input_trans_type);
  dc_task.set_output_trans_type(output_trans_type);
  dc_task.set_input_slew(input_slew);
  STADC.calculate(dc_task);
  if (!dc_task.get_is_valid()) {
    return;
  }
  double arc_delay = dc_task.get_timing_result().get_delay();
  double output_slew = dc_task.get_timing_result().get_slew();
  updateGraphArcDelay(arc, analysis_type, input_trans_type, output_trans_type, arc_delay);
  if (sink_point.get_clock_slew_map().count(analysis_type) == 0
      || sink_point.get_clock_slew_map()[analysis_type].count(output_trans_type) == 0
      || isBetterSlew(output_slew, sink_point.get_clock_slew_map()[analysis_type][output_trans_type], analysis_type)) {
    sink_point.get_clock_slew_map()[analysis_type][output_trans_type] = output_slew;
  }
}

void ClockPropagator::propagateClockArrivalArc(std::size_t arc_idx, AnalysisType analysis_type)
{
  propagateClockArrivalArc(arc_idx, analysis_type, TransType::kRise);
  propagateClockArrivalArc(arc_idx, analysis_type, TransType::kFall);
}

void ClockPropagator::propagateClockArrivalArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = STADM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  if (!source_point.get_is_clock_point() || !sink_point.get_is_clock_point()) {
    return;
  }
  if (!hasClockArrival(source_point, analysis_type, input_trans_type)) {
    return;
  }
  if (source_point.get_clock_slew_map().count(analysis_type) == 0 || source_point.get_clock_slew_map()[analysis_type].count(input_trans_type) == 0) {
    return;
  }

  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    updateClockPathState(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void ClockPropagator::updateClockPathState(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                                            TransType input_trans_type, TransType output_trans_type)
{
  double arc_delay = getArcDelay(arc, analysis_type, input_trans_type, output_trans_type);
  double candidate_arrival = roundTime(getClockArrival(source_point, analysis_type, input_trans_type) + arc_delay);
  if (!hasClockArrival(sink_point, analysis_type, output_trans_type)
      || isBetterArrival(candidate_arrival, getClockArrival(sink_point, analysis_type, output_trans_type), analysis_type)) {
    updateClockArrival(sink_point, analysis_type, output_trans_type, candidate_arrival);
    updateClockPredecessor(sink_point, analysis_type, output_trans_type, input_trans_type, arc, arc_delay);
  }
}

bool ClockPropagator::hasClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  return timing_point.get_clock_arrival_map().count(analysis_type) > 0 && timing_point.get_clock_arrival_map()[analysis_type].count(trans_type) > 0;
}

double ClockPropagator::getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (!hasClockArrival(timing_point, analysis_type, trans_type)) {
    return 0.0;
  }
  return timing_point.get_clock_arrival_map()[analysis_type][trans_type];
}

void ClockPropagator::updateClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double clock_arrival)
{
  timing_point.get_clock_arrival_map()[analysis_type][trans_type] = clock_arrival;
}

void ClockPropagator::updateClockPredecessor(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, TransType predecessor_trans_type,
                                              Arc& arc, double arc_delay)
{
  timing_point.get_clock_predecessor_map()[analysis_type][trans_type] = arc.get_source_pin();
  timing_point.get_clock_predecessor_arc_delay_map()[analysis_type][trans_type] = arc_delay;
  timing_point.get_clock_predecessor_trans_type_map()[analysis_type][trans_type] = predecessor_trans_type;
}

bool ClockPropagator::shouldStopClockPropagation(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

void ClockPropagator::updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay)
{
  if (arc.get_graph_delay_map().count(analysis_type) == 0 || arc.get_graph_delay_map()[analysis_type].count(input_trans_type) == 0
      || arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) == 0
      || isBetterDelay(arc_delay, arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type], analysis_type)) {
    arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type] = arc_delay;
  }
}

bool ClockPropagator::isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_delay < current_delay - STA_ERROR;
  }
  return candidate_delay > current_delay + STA_ERROR;
}

bool ClockPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - STA_ERROR;
  }
  return candidate_slew > current_slew + STA_ERROR;
}

double ClockPropagator::roundTime(double time)
{
  return std::round(time * 1E15) / 1E15;
}
std::vector<TransType> ClockPropagator::getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
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

double ClockPropagator::getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (arc.get_trans_delay_map().count(analysis_type) > 0 && arc.get_trans_delay_map()[analysis_type].count(input_trans_type) > 0) {
    return arc.get_trans_delay_map()[analysis_type][input_trans_type];
  }
  if (analysis_type == AnalysisType::kMin) {
    return arc.get_delay_min();
  }
  return arc.get_delay_max();
}

double ClockPropagator::getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  if (arc.get_graph_delay_map().count(analysis_type) > 0 && arc.get_graph_delay_map()[analysis_type].count(input_trans_type) > 0
      && arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) > 0) {
    return arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type];
  }
  if (arc.get_input_output_delay_map().count(analysis_type) > 0 && arc.get_input_output_delay_map()[analysis_type].count(input_trans_type) > 0
      && arc.get_input_output_delay_map()[analysis_type][input_trans_type].count(output_trans_type) > 0) {
    return arc.get_input_output_delay_map()[analysis_type][input_trans_type][output_trans_type];
  }
  return getArcDelay(arc, analysis_type, input_trans_type);
}

bool ClockPropagator::isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type)
{
  if (!isFinite(current_arrival)) {
    return true;
  }
  if (analysis_type == AnalysisType::kMin) {
    return candidate_arrival < current_arrival - STA_ERROR;
  }
  return candidate_arrival > current_arrival + STA_ERROR;
}

bool ClockPropagator::isFinite(double value)
{
  return std::isfinite(value);
}

}  // namespace ista
