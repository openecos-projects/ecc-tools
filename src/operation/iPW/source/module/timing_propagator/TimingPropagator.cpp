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
#include "TimingPropagator.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "TimingCaseAnalysis.hpp"
#include "TimingExceptionMatcher.hpp"
#include "Utility.hpp"

namespace ipw {

// public

void TimingPropagator::initInst()
{
  if (_tp_instance == nullptr) {
    _tp_instance = new TimingPropagator();
  }
}

TimingPropagator& TimingPropagator::getInst()
{
  if (_tp_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_tp_instance;
}

void TimingPropagator::destroyInst()
{
  if (_tp_instance != nullptr) {
    delete _tp_instance;
    _tp_instance = nullptr;
  }
}

// function

void TimingPropagator::propagate()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  Database& database = PWDM.getDatabase();
  TimingCaseAnalysis::apply(database);
  TPModel tp_model = initTPModel();
  buildStartPointList(tp_model);
  seedStartPointList(tp_model);
  propagateDataSlewDelay(tp_model);
  for (std::string& pin_name : database.get_timing_order_list()) {
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (isDisableArc(database.get_arc_list()[arc_idx])) {
        continue;
      }
      if (shouldStopDataPropagation(database.get_arc_list()[arc_idx])) {
        continue;
      }
      propagateArrivalArc(arc_idx);
    }
  }

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

TimingPropagator* TimingPropagator::_tp_instance = nullptr;

bool TimingPropagator::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

bool TimingPropagator::shouldStopDataPropagation(Arc& arc)
{
  return arc.get_type() == ArcType::kNet && isSequentialClockPin(arc.get_sink_pin());
}

bool TimingPropagator::shouldStopDataSlewPropagation(Arc& arc)
{
  if (arc.get_type() != ArcType::kNet || !isSequentialClockPin(arc.get_sink_pin())) {
    return false;
  }
  return PWDM.getDatabase().get_timing_point_map()[arc.get_sink_pin()].get_is_clock_point();
}

bool TimingPropagator::isSequentialClockPin(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_clock_pin_name();
}

bool TimingPropagator::hasIncomingPhysicalSlewArc(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (std::size_t arc_idx : database.get_incoming_arc_list_map()[pin_name]) {
    Arc& arc = database.get_arc_list()[arc_idx];
    if (arc.get_type() == ArcType::kNet && !isDisableArc(arc)) {
      return true;
    }
  }
  return false;
}

TPModel TimingPropagator::initTPModel()
{
  TPModel tp_model;
  return tp_model;
}

void TimingPropagator::buildStartPointList(TPModel& tp_model)
{
  tp_model.set_start_point_list(PWDM.getDatabase().get_start_point_list());
}

double TimingPropagator::getClockArrival(std::string& pin_name, AnalysisType analysis_type)
{
  return getClockArrival(pin_name, analysis_type, TransType::kRise);
}

double TimingPropagator::getClockArrival(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  return getClockArrival(timing_point, analysis_type, trans_type);
}

double TimingPropagator::getClockArrival(std::string& pin_name, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  const auto point = database.get_timing_point_map().find(pin_name);
  if (point == database.get_timing_point_map().end()) return 0.0;
  const TimingClockPointState* state = point->second.find_clock_state(clock_name);
  if (state == nullptr || !state->arrival_map.contains(analysis_type) || !state->arrival_map.at(analysis_type).contains(trans_type)) return 0.0;
  return state->arrival_map.at(analysis_type).at(trans_type);
}

double TimingPropagator::getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (timing_point.get_clock_arrival_map().count(analysis_type) == 0 || timing_point.get_clock_arrival_map()[analysis_type].count(trans_type) == 0) {
    return 0.0;
  }
  return timing_point.get_clock_arrival_map()[analysis_type][trans_type];
}

double TimingPropagator::getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  if (timing_point.get_clock_slew_map().count(analysis_type) == 0 || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0) {
    return 0.0;
  }
  return timing_point.get_clock_slew_map()[analysis_type][trans_type];
}

double TimingPropagator::getClockSlew(std::string& pin_name, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  const auto point = database.get_timing_point_map().find(pin_name);
  if (point == database.get_timing_point_map().end()) return 0.0;
  const TimingClockPointState* state = point->second.find_clock_state(clock_name);
  if (state == nullptr || !state->slew_map.contains(analysis_type) || !state->slew_map.at(analysis_type).contains(trans_type)) return 0.0;
  return state->slew_map.at(analysis_type).at(trans_type);
}

void TimingPropagator::seedStartPointList(TPModel& tp_model)
{
  Database& database = PWDM.getDatabase();
  for (std::string& start_point : tp_model.get_start_point_list()) {
    TimingPoint& timing_point = database.get_timing_point_map()[start_point];
    timing_point.set_arrival(getStartPointArrival(start_point, AnalysisType::kMax));
    timing_point.set_launch_time(getStartPointLaunchTime(start_point, AnalysisType::kMax));
    timing_point.set_clock_name(getClockName(start_point));
    seedPathState(start_point, AnalysisType::kMax);
    seedPathState(start_point, AnalysisType::kMin);
  }
}

void TimingPropagator::propagateDataSlewDelay(TPModel& tp_model)
{
  Database& database = PWDM.getDatabase();
  seedDataSlewList(tp_model);
  for (std::string& pin_name : database.get_timing_order_list()) {
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (isDisableArc(database.get_arc_list()[arc_idx])) {
        continue;
      }
      propagateDataSlewDelayArc(arc_idx);
    }
  }
}

void TimingPropagator::seedDataSlewList(TPModel& tp_model)
{
  for (std::string& start_point : tp_model.get_start_point_list()) {
    seedDataSlew(start_point, AnalysisType::kMax);
    seedDataSlew(start_point, AnalysisType::kMin);
  }
}

void TimingPropagator::seedDataSlew(std::string& start_point, AnalysisType analysis_type)
{
  seedDataSlew(start_point, analysis_type, TransType::kRise);
  seedDataSlew(start_point, analysis_type, TransType::kFall);
}

void TimingPropagator::seedDataSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (!TimingCaseAnalysis::allowsTransition(database, start_point, trans_type)) {
    return;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[start_point];
  if (isSequentialClockPin(start_point) && hasIncomingPhysicalSlewArc(start_point)
      && (timing_point.get_clock_slew_map().count(analysis_type) == 0 || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0)) {
    // A propagated divider or gated-clock path will provide the physical slew
    // through this net arc. Seeding zero here would win min-slew selection.
    return;
  }
  timing_point.get_data_slew_map()[analysis_type][trans_type] = getStartPointSlew(start_point, analysis_type, trans_type);
}

void TimingPropagator::propagateDataSlewDelayArc(std::size_t arc_idx)
{
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kRise);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMax, TransType::kFall);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kRise);
  propagateDataSlewDelayArc(arc_idx, AnalysisType::kMin, TransType::kFall);
}

void TimingPropagator::propagateDataSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = PWDM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  if (shouldStopDataSlewPropagation(arc)) {
    return;
  }
  if (!TimingCaseAnalysis::allowsTransition(database, arc.get_source_pin(), input_trans_type)) {
    return;
  }
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  if (!hasDataSlew(source_point, analysis_type, input_trans_type)) {
    return;
  }
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    if (!TimingCaseAnalysis::allowsTransition(database, arc.get_sink_pin(), output_trans_type)) {
      continue;
    }
    updateDataSlewDelay(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void TimingPropagator::updateDataSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type, TransType input_trans_type,
                                           TransType output_trans_type)
{
  double input_slew = getDataSlew(source_point, analysis_type, input_trans_type);
  DCTask dc_task;
  dc_task.set_proc_type(DCProcType::kCalculate);
  dc_task.set_arc(&arc);
  dc_task.set_analysis_type(analysis_type);
  dc_task.set_input_trans_type(input_trans_type);
  dc_task.set_output_trans_type(output_trans_type);
  dc_task.set_input_slew(input_slew);
  if (isSequentialClockPin(arc.get_source_pin())) {
    // PT uses the ordinary propagated slew for C2Q delay, but uses the clock
    // slew for its output-transition lookup.  A CK reached through data logic
    // has no clock slew, so the latter intentionally falls back to zero.
    dc_task.set_output_slew_input_slew(getClockSlew(arc.get_source_pin(), analysis_type, input_trans_type));
  }
  PWDC.calculate(dc_task);
  if (!dc_task.get_is_valid()) {
    return;
  }
  double arc_delay = dc_task.get_timing_result().get_delay();
  double output_slew = dc_task.get_timing_result().get_slew();
  updateGraphArcDelay(arc, analysis_type, input_trans_type, output_trans_type, arc_delay);
  updateDataSlew(sink_point, analysis_type, output_trans_type, output_slew);
}

void TimingPropagator::updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay)
{
  if (arc.get_graph_delay_map().count(analysis_type) == 0 || arc.get_graph_delay_map()[analysis_type].count(input_trans_type) == 0
      || arc.get_graph_delay_map()[analysis_type][input_trans_type].count(output_trans_type) == 0
      || isBetterDelay(arc_delay, arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type], analysis_type)) {
    arc.get_graph_delay_map()[analysis_type][input_trans_type][output_trans_type] = arc_delay;
  }
}

void TimingPropagator::updateDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double data_slew)
{
  if (!hasDataSlew(timing_point, analysis_type, trans_type)
      || isBetterSlew(data_slew, timing_point.get_data_slew_map()[analysis_type][trans_type], analysis_type)) {
    timing_point.get_data_slew_map()[analysis_type][trans_type] = data_slew;
  }
}

bool TimingPropagator::hasDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  return timing_point.get_data_slew_map().count(analysis_type) > 0 && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0;
}

double TimingPropagator::getDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (!hasDataSlew(timing_point, analysis_type, trans_type)) {
    return 0.0;
  }
  return timing_point.get_data_slew_map()[analysis_type][trans_type];
}

bool TimingPropagator::isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_delay < current_delay - PW_ERROR;
  }
  return candidate_delay > current_delay + PW_ERROR;
}

bool TimingPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - PW_ERROR;
  }
  return candidate_slew > current_slew + PW_ERROR;
}

double TimingPropagator::roundTime(double time)
{
  return std::round(time * 1E15) / 1E15;
}

double TimingPropagator::getStartPointArrival(std::string& start_point, AnalysisType analysis_type)
{
  return getStartPointArrival(start_point, analysis_type, TransType::kRise);
}

double TimingPropagator::getStartPointArrival(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (isClockSourceStartPoint(start_point)) {
    return getStartPointClockEdge(start_point, analysis_type, trans_type);
  }
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port()) {
    double arrival = 0.0;
    const std::vector<const TimingIoDelay*> delays = getInputDelayList(start_point, analysis_type, trans_type);
    bool has_arrival = false;
    for (const TimingIoDelay* delay : delays) {
      const double candidate = getInputDelayArrival(*delay);
      if (!has_arrival || isBetterArrival(candidate, arrival, analysis_type)) {
        arrival = candidate;
        has_arrival = true;
      }
    }
    if (!has_arrival) {
      std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
      if (analysis_type == AnalysisType::kMin && port_constraint_map.count(start_point) > 0
          && port_constraint_map[start_point].get_has_input_delay_min()) {
        arrival = PWUTIL.getLaunchClockEdge(database, start_point, getClockName(start_point)) + port_constraint_map[start_point].get_input_delay_min();
      } else if (port_constraint_map.count(start_point) > 0 && port_constraint_map[start_point].get_has_input_delay_max()) {
        arrival = PWUTIL.getLaunchClockEdge(database, start_point, getClockName(start_point)) + port_constraint_map[start_point].get_input_delay_max();
      }
    }
    std::optional<DCTimingResult> driving_cell_timing = getDrivingCellTiming(start_point, analysis_type, trans_type);
    return driving_cell_timing ? arrival + driving_cell_timing->get_delay() : arrival;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    return PWUTIL.getLaunchClockEdge(database, start_point, getClockName(start_point))
           + getClockArrival(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
  }
  if (instance.get_is_sequential() && start_point == instance.get_output_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    double clock_slew = getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
    DCTask dc_task;
    dc_task.set_proc_type(DCProcType::kCalculate);
    dc_task.set_timing_cell_arc(&instance.get_clock_to_q_arc());
    dc_task.set_output_pin(start_point);
    dc_task.set_analysis_type(analysis_type);
    dc_task.set_input_trans_type(clock_trans_type);
    dc_task.set_output_trans_type(trans_type);
    dc_task.set_input_slew(clock_slew);
    PWDC.calculate(dc_task);
    if (dc_task.get_is_valid()) {
      return PWUTIL.getLaunchClockEdge(database, start_point, getClockName(start_point))
             + getClockArrival(instance.get_clock_pin_name(), analysis_type, clock_trans_type) + dc_task.get_timing_result().get_delay();
    }
  }
  return 0.0;
}

double TimingPropagator::getStartPointArrival(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || !database.get_instance_map().contains(pin.get_instance_name())) {
    return getStartPointArrival(start_point, analysis_type, trans_type);
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_clock_pin_name() && start_point != instance.get_output_pin_name())) {
    return getStartPointArrival(start_point, analysis_type, trans_type);
  }
  const TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
  const double clock_arrival = getClockEdge(clock_name, clock_trans_type)
                               + getClockArrival(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type);
  if (start_point == instance.get_clock_pin_name()) return clock_arrival;

  DCTask dc_task;
  dc_task.set_proc_type(DCProcType::kCalculate);
  dc_task.set_timing_cell_arc(&instance.get_clock_to_q_arc());
  dc_task.set_output_pin(start_point);
  dc_task.set_analysis_type(analysis_type);
  dc_task.set_input_trans_type(clock_trans_type);
  dc_task.set_output_trans_type(trans_type);
  dc_task.set_input_slew(getClockSlew(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type));
  PWDC.calculate(dc_task);
  return dc_task.get_is_valid() ? clock_arrival + dc_task.get_timing_result().get_delay() : clock_arrival;
}

std::vector<const TimingIoDelay*> TimingPropagator::getInputDelayList(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  const auto constraint = database.get_timing_constraint().get_port_constraint_map().find(start_point);
  if (constraint == database.get_timing_constraint().get_port_constraint_map().end()) {
    return {};
  }
  std::vector<const TimingIoDelay*> delays = constraint->second.get_input_delays(analysis_type, trans_type);
  if (delays.empty() && analysis_type == AnalysisType::kMin) {
    delays = constraint->second.get_input_delays(AnalysisType::kMax, trans_type);
  }
  return delays;
}

double TimingPropagator::getClockEdge(std::string_view clock_name, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  const auto clock = database.get_timing_constraint().get_clock_map().find(std::string(clock_name));
  if (clock == database.get_timing_constraint().get_clock_map().end()) {
    return 0.0;
  }
  return trans_type == TransType::kFall ? clock->second.get_fall_edge() : clock->second.get_rise_edge();
}

double TimingPropagator::getInputDelayArrival(const TimingIoDelay& delay)
{
  return getClockEdge(delay.get_clock_name(), delay.get_clock_trans_type()) + delay.get_delay();
}

bool TimingPropagator::isClockSourceStartPoint(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  return pin.get_is_port() && getStartPointClock(start_point) != nullptr;
}

TimingClock* TimingPropagator::getStartPointClock(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    for (std::string& clock_source : clock_pair.second.get_source_list()) {
      if (clock_source == start_point) {
        return &clock_pair.second;
      }
    }
  }
  return nullptr;
}

double TimingPropagator::getStartPointClockEdge(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  TimingClock* timing_clock = getStartPointClock(start_point);
  if (timing_clock == nullptr) {
    return 0.0;
  }
  if (analysis_type == AnalysisType::kMax && trans_type == TransType::kFall) {
    return timing_clock->get_fall_edge();
  }
  return timing_clock->get_rise_edge();
}

double TimingPropagator::getStartPointSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    if (port_constraint_map.count(start_point) == 0) {
      return 0.0;
    }
    TimingPortConstraint& port_constraint = port_constraint_map[start_point];
    if (port_constraint.get_has_driving_cell(analysis_type, trans_type)) {
      std::optional<DCTimingResult> driving_cell_timing = getDrivingCellTiming(start_point, analysis_type, trans_type);
      return driving_cell_timing ? driving_cell_timing->get_slew() : 0.0;
    }
    if (port_constraint.get_has_input_transition(analysis_type, trans_type)) {
      return port_constraint.get_input_transition(analysis_type, trans_type);
    }
    return 0.0;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    return getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
  }
  if (instance.get_is_sequential() && start_point == instance.get_output_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    double clock_slew = getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
    DCTask dc_task;
    dc_task.set_proc_type(DCProcType::kCalculate);
    dc_task.set_timing_cell_arc(&instance.get_clock_to_q_arc());
    dc_task.set_output_pin(start_point);
    dc_task.set_analysis_type(analysis_type);
    dc_task.set_input_trans_type(clock_trans_type);
    dc_task.set_output_trans_type(trans_type);
    dc_task.set_input_slew(clock_slew);
    PWDC.calculate(dc_task);
    if (dc_task.get_is_valid()) {
      return dc_task.get_timing_result().get_slew();
    }
  }
  return 0.0;
}

double TimingPropagator::getStartPointSlew(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || !database.get_instance_map().contains(pin.get_instance_name())) {
    return getStartPointSlew(start_point, analysis_type, trans_type);
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_clock_pin_name() && start_point != instance.get_output_pin_name())) {
    return getStartPointSlew(start_point, analysis_type, trans_type);
  }
  const TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
  const double clock_slew = getClockSlew(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type);
  if (start_point == instance.get_clock_pin_name()) return clock_slew;

  DCTask dc_task;
  dc_task.set_proc_type(DCProcType::kCalculate);
  dc_task.set_timing_cell_arc(&instance.get_clock_to_q_arc());
  dc_task.set_output_pin(start_point);
  dc_task.set_analysis_type(analysis_type);
  dc_task.set_input_trans_type(clock_trans_type);
  dc_task.set_output_trans_type(trans_type);
  dc_task.set_input_slew(clock_slew);
  PWDC.calculate(dc_task);
  return dc_task.get_is_valid() ? dc_task.get_timing_result().get_slew() : clock_slew;
}

std::optional<DCTimingResult> TimingPropagator::getDrivingCellTiming(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  if (!port_constraint_map.contains(start_point)) {
    return std::nullopt;
  }
  TimingDrivingCell* driving_cell = port_constraint_map.at(start_point).get_driving_cell(analysis_type, trans_type);
  if (driving_cell == nullptr) {
    return std::nullopt;
  }
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (!timing_cell_map.contains(driving_cell->get_cell_name())) {
    return std::nullopt;
  }
  TimingCell& timing_cell = timing_cell_map.at(driving_cell->get_cell_name());
  if (timing_cell.get_library_name() != driving_cell->get_library_name()) {
    return std::nullopt;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() != driving_cell->get_from_pin() || timing_cell_arc.get_sink_port() != driving_cell->get_to_pin()) {
      continue;
    }
    DCTimingResult timing_result;
    if (PWDC.calculateDrivingCell(start_point, timing_cell_arc, analysis_type, trans_type, driving_cell->get_input_transition_rise(),
                                   driving_cell->get_input_transition_fall(), timing_result)) {
      return timing_result;
    }
  }
  return std::nullopt;
}

double TimingPropagator::getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type)
{
  return getStartPointLaunchTime(start_point, analysis_type, TransType::kRise);
}

double TimingPropagator::getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (isClockSourceStartPoint(start_point)) {
    return getStartPointClockEdge(start_point, analysis_type, trans_type);
  }
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return PWUTIL.getLaunchClockEdge(database, start_point, getClockName(start_point));
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return 0.0;
  }
  return PWUTIL.getLaunchClockEdge(database, start_point, getClockName(start_point))
         + getClockArrival(instance.get_clock_pin_name(), analysis_type, getClockTransType(instance.get_clock_to_q_arc()));
}

double TimingPropagator::getStartPointLaunchTime(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || !database.get_instance_map().contains(pin.get_instance_name())) {
    return getClockEdge(clock_name, trans_type);
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) return 0.0;
  const TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
  return getClockEdge(clock_name, clock_trans_type)
         + getClockArrival(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type);
}

std::string TimingPropagator::getStartPointCrprClockPin(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return "";
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return "";
  }
  std::string clock_pin_name = instance.get_clock_pin_name();
  return clock_pin_name;
}

TransType TimingPropagator::getStartPointCrprClockTransType(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return TransType::kNone;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return TransType::kNone;
  }
  return getClockTransType(instance.get_clock_to_q_arc());
}

TransType TimingPropagator::getClockTransType(TimingCellArc& timing_cell_arc)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return TransType::kRise;
  }
  TimingArc& timing_arc = timing_cell_arc.get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() != TransType::kNone) {
    return timing_arc.get_trigger_trans_type();
  }
  return TransType::kRise;
}

std::string_view TimingPropagator::getClockName(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  TimingClock* timing_clock = getStartPointClock(pin_name);
  if (timing_clock != nullptr) {
    return timing_clock->get_clock_name();
  }
  Pin& pin = database.get_pin_map()[pin_name];
  if (!pin.get_is_port() && database.get_instance_map().count(pin.get_instance_name()) > 0) {
    Instance& instance = database.get_instance_map()[pin.get_instance_name()];
    if (instance.get_is_sequential() && database.get_timing_point_map().count(instance.get_clock_pin_name()) > 0) {
      const auto& propagated_clock_name = database.get_timing_point_map()[instance.get_clock_pin_name()].get_clock_name();
      if (!propagated_clock_name.empty()) {
        return propagated_clock_name;
      }
    }
  }
  if (database.get_timing_point_map().count(pin_name) > 0) {
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    if (timing_point.get_is_clock_point() && !timing_point.get_clock_name().empty()) {
      return timing_point.get_clock_name();
    }
  }
  std::map<std::string, TimingClock>& clock_map = database.get_timing_constraint().get_clock_map();
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    if (port_constraint_map.count(pin_name) > 0 && !port_constraint_map[pin_name].get_clock_name().empty()) {
      return port_constraint_map[pin_name].get_clock_name();
    }
  }
  if (!clock_map.empty()) {
    return clock_map.begin()->first;
  }
  return "clk";
}

std::vector<std::string> TimingPropagator::getStartPointClockNames(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  std::set<std::string> names;
  for (const auto& [clock_name, clock] : database.get_timing_constraint().get_clock_map()) {
    if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), start_point) != clock.get_source_list().end()) names.insert(clock_name);
  }
  Pin& pin = database.get_pin_map()[start_point];
  if (!pin.get_is_port() && database.get_instance_map().contains(pin.get_instance_name())) {
    Instance& instance = database.get_instance_map()[pin.get_instance_name()];
    if (instance.get_is_sequential() && database.get_timing_point_map().contains(instance.get_clock_pin_name())) {
      const auto& states = database.get_timing_point_map().at(instance.get_clock_pin_name()).get_clock_state_map();
      for (const auto& [clock_name, state] : states) names.insert(clock_name);
    }
  }
  if (names.empty()) names.insert(std::string(getClockName(start_point)));
  return {names.begin(), names.end()};
}

std::string TimingPropagator::getPathStateStartPoint(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return start_point;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    return instance.get_output_pin_name();
  }
  return start_point;
}

void TimingPropagator::seedPathState(std::string& start_point, AnalysisType analysis_type)
{
  PathSourceType source_type = getStartPointSourceType(start_point, analysis_type);
  if (source_type == PathSourceType::kNone) {
    return;
  }
  Database& database = PWDM.getDatabase();
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    if (!TimingCaseAnalysis::allowsTransition(database, start_point, trans_type)) {
      continue;
    }
    const std::vector<const TimingIoDelay*> input_delays = getInputDelayList(start_point, analysis_type, trans_type);
    if (isInputStartPoint(start_point) && !input_delays.empty()) {
      for (const TimingIoDelay* delay : input_delays) {
        seedInputPathState(start_point, analysis_type, trans_type, *delay);
      }
      continue;
    }
    for (const std::string& clock_name : getStartPointClockNames(start_point)) {
      seedPathState(start_point, analysis_type, trans_type, clock_name,
                    getStartPointArrival(start_point, clock_name, analysis_type, trans_type),
                    getStartPointSlew(start_point, clock_name, analysis_type, trans_type),
                    getStartPointLaunchTime(start_point, clock_name, analysis_type, trans_type), getStartPointCrprClockTransType(start_point));
    }
  }
}

void TimingPropagator::seedInputPathState(std::string& start_point, AnalysisType analysis_type, TransType trans_type, const TimingIoDelay& delay)
{
  const double arrival = getInputDelayArrival(delay);
  const double launch_time = getClockEdge(delay.get_clock_name(), delay.get_clock_trans_type());
  const std::string clock_name = delay.get_clock_name().empty() ? std::string(getClockName(start_point)) : delay.get_clock_name();
  std::optional<DCTimingResult> driving_cell_timing = getDrivingCellTiming(start_point, analysis_type, trans_type);
  seedPathState(start_point, analysis_type, trans_type, clock_name, driving_cell_timing ? arrival + driving_cell_timing->get_delay() : arrival,
                driving_cell_timing ? driving_cell_timing->get_slew() : getStartPointSlew(start_point, analysis_type, trans_type), launch_time,
                delay.get_clock_trans_type());
}

void TimingPropagator::seedPathState(std::string& start_point, AnalysisType analysis_type, TransType trans_type, std::string_view clock_name, double arrival,
                                     double slew, double launch_time, TransType clock_trans_type)
{
  Database& database = PWDM.getDatabase();
  PathSourceType source_type = getStartPointSourceType(start_point, analysis_type);
  if (source_type == PathSourceType::kNone) {
    return;
  }
  std::string path_state_start_point = getPathStateStartPoint(start_point);
  TimingPoint& timing_point = database.get_timing_point_map()[start_point];
  const std::vector<int32_t> initial_exception_state_list
      = TimingExceptionMatcher::initState(database, path_state_start_point, clock_name, trans_type, analysis_type);
  const std::vector<int32_t> exception_state_list
      = TimingExceptionMatcher::advanceState(database, initial_exception_state_list, start_point, trans_type);
  const std::string path_state_tag = TimingExceptionMatcher::makePathStateTag(database, path_state_start_point, clock_name, exception_state_list);
  std::map<std::string, TimingPathState>& path_state_map = getPathStateMap(timing_point, analysis_type, source_type, trans_type);
  if (path_state_map.count(path_state_tag) > 0 && !isBetterArrival(arrival, path_state_map[path_state_tag].get_arrival(), analysis_type)) {
    return;
  }

  TimingPathState& path_state = path_state_map[path_state_tag];
  path_state.set_arrival(arrival);
  path_state.set_slew(slew);
  path_state.set_launch_time(launch_time);
  path_state.set_start_point(path_state_start_point);
  path_state.set_clock_name(clock_name);
  path_state.set_crpr_clock_pin(getStartPointCrprClockPin(start_point));
  path_state.set_path_state_tag(path_state_tag);
  path_state.set_predecessor_path_state_tag("");
  path_state.set_exception_state_list(exception_state_list);
  path_state.get_predecessor().clear();
  path_state.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
  path_state.set_predecessor_arc_delay(0.0);
  path_state.set_trans_type(trans_type);
  path_state.set_predecessor_trans_type(TransType::kNone);
  path_state.set_crpr_clock_trans_type(clock_trans_type);
}

PathSourceType TimingPropagator::getStartPointSourceType(std::string& start_point, AnalysisType analysis_type)
{
  if (isClockSourceStartPoint(start_point)) {
    return PathSourceType::kInput;
  }
  if (isInputStartPoint(start_point) && hasInputDelay(start_point, analysis_type)) {
    return PathSourceType::kInput;
  }
  if (isRegisterStartPoint(start_point)) {
    return PathSourceType::kRegister;
  }
  return PathSourceType::kNone;
}

bool TimingPropagator::hasInputDelay(std::string& start_point, AnalysisType analysis_type)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  if (port_constraint_map.count(start_point) == 0) {
    return false;
  }
  TimingPortConstraint& port_constraint = port_constraint_map[start_point];
  if (analysis_type == AnalysisType::kMin) {
    return port_constraint.get_has_input_delay_min() || port_constraint.get_has_input_delay_max();
  }
  return port_constraint.get_has_input_delay_max() || port_constraint.get_has_input_delay_min();
}

bool TimingPropagator::isInputStartPoint(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

bool TimingPropagator::isRegisterStartPoint(std::string& start_point)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && (start_point == instance.get_output_pin_name() || start_point == instance.get_clock_pin_name())
         && hasClockPoint(instance.get_clock_pin_name());
}

bool TimingPropagator::hasClockPoint(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  return database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point();
}

void TimingPropagator::propagateArrivalArc(std::size_t arc_idx)
{
  Database& database = PWDM.getDatabase();
  const double kEpsilon = 1e-9;
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  if (isFinite(source_point.get_arrival())) {
    const double candidate_arrival = source_point.get_arrival() + arc.get_delay();
    if (!isFinite(sink_point.get_arrival()) || candidate_arrival > sink_point.get_arrival() + kEpsilon) {
      sink_point.set_arrival(candidate_arrival);
      sink_point.set_predecessor(arc.get_source_pin());
      sink_point.set_predecessor_arc_idx(arc_idx);
      sink_point.set_launch_time(source_point.get_launch_time());
      sink_point.set_clock_name(source_point.get_clock_name());
    }
  }
  propagatePathStateArc(arc_idx, AnalysisType::kMax, PathSourceType::kInput);
  propagatePathStateArc(arc_idx, AnalysisType::kMax, PathSourceType::kRegister);
  propagatePathStateArc(arc_idx, AnalysisType::kMin, PathSourceType::kInput);
  propagatePathStateArc(arc_idx, AnalysisType::kMin, PathSourceType::kRegister);
}

void TimingPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type)
{
  propagatePathStateArc(arc_idx, analysis_type, source_type, TransType::kRise);
  propagatePathStateArc(arc_idx, analysis_type, source_type, TransType::kFall);
}

void TimingPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type)
{
  Database& database = PWDM.getDatabase();
  if (isDisableArc(database.get_arc_list()[arc_idx])) {
    return;
  }
  Arc& arc = database.get_arc_list()[arc_idx];
  if (!TimingCaseAnalysis::allowsTransition(database, arc.get_source_pin(), input_trans_type)) {
    return;
  }
  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    if (!TimingCaseAnalysis::allowsTransition(database, arc.get_sink_pin(), output_trans_type)) {
      continue;
    }
    propagatePathStateArc(arc_idx, analysis_type, source_type, input_trans_type, output_trans_type);
  }
}

void TimingPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type,
                                             TransType output_trans_type)
{
  Database& database = PWDM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  if (!TimingCaseAnalysis::allowsTransition(database, arc.get_source_pin(), input_trans_type)
      || !TimingCaseAnalysis::allowsTransition(database, arc.get_sink_pin(), output_trans_type)) {
    return;
  }
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  if (!hasPathState(source_point, analysis_type, source_type, input_trans_type)) {
    return;
  }

  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  std::map<std::string, TimingPathState>& source_path_state_map = getPathStateMap(source_point, analysis_type, source_type, input_trans_type);
  for (std::pair<const std::string, TimingPathState>& source_path_state_pair : source_path_state_map) {
    TimingPathState& source_path_state = source_path_state_pair.second;
    if (!isFinite(source_path_state.get_arrival())) {
      continue;
    }
    double arc_delay = getArcDelay(arc, analysis_type, input_trans_type, output_trans_type);
    double candidate_arrival = roundTime(source_path_state.get_arrival() + arc_delay);
    const std::vector<int32_t> exception_state_list
        = TimingExceptionMatcher::advanceState(database, source_path_state.get_exception_state_list(), arc.get_sink_pin(), output_trans_type);
    const std::string path_state_tag
        = TimingExceptionMatcher::makePathStateTag(database, source_path_state.get_start_point(), source_path_state.get_clock_name(), exception_state_list);
    std::map<std::string, TimingPathState>& sink_path_state_map = getPathStateMap(sink_point, analysis_type, source_type, output_trans_type);
    if (sink_path_state_map.count(path_state_tag) == 0
        || isBetterArrival(candidate_arrival, sink_path_state_map[path_state_tag].get_arrival(), analysis_type)) {
      TimingPathState& sink_path_state = sink_path_state_map[path_state_tag];
      sink_path_state.set_arrival(candidate_arrival);
      sink_path_state.set_slew(getDataSlew(sink_point, analysis_type, output_trans_type));
      sink_path_state.set_start_point(source_path_state.get_start_point());
      sink_path_state.set_predecessor(arc.get_source_pin());
      sink_path_state.set_predecessor_arc_idx(arc_idx);
      sink_path_state.set_predecessor_arc_delay(arc_delay);
      sink_path_state.set_launch_time(source_path_state.get_launch_time());
      sink_path_state.set_clock_name(source_path_state.get_clock_name());
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

std::vector<TransType> TimingPropagator::getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
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

double TimingPropagator::getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (arc.get_trans_delay_map().count(analysis_type) > 0 && arc.get_trans_delay_map()[analysis_type].count(input_trans_type) > 0) {
    return arc.get_trans_delay_map()[analysis_type][input_trans_type];
  }
  if (analysis_type == AnalysisType::kMin) {
    return arc.get_delay_min();
  }
  return arc.get_delay_max();
}

double TimingPropagator::getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
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

bool TimingPropagator::hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type)
{
  return hasPathState(timing_point, analysis_type, source_type, TransType::kRise) || hasPathState(timing_point, analysis_type, source_type, TransType::kFall);
}

bool TimingPropagator::hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type)
{
  return timing_point.get_path_state_map().count(analysis_type) > 0 && timing_point.get_path_state_map()[analysis_type].count(source_type) > 0
         && timing_point.get_path_state_map()[analysis_type][source_type].count(trans_type) > 0
         && !timing_point.get_path_state_map()[analysis_type][source_type][trans_type].empty();
}

std::map<std::string, TimingPathState>& TimingPropagator::getPathStateMap(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type,
                                                                          TransType trans_type)
{
  return timing_point.get_path_state_map()[analysis_type][source_type][trans_type];
}

TimingPathState& TimingPropagator::getPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                                std::string& path_state_tag)
{
  return timing_point.get_path_state_map()[analysis_type][source_type][trans_type][path_state_tag];
}

TimingPathState* TimingPropagator::getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type)
{
  TimingPathState* best_path_state = nullptr;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    TimingPathState* path_state = getWorstPathState(timing_point, analysis_type, source_type, trans_type);
    if (path_state == nullptr) {
      continue;
    }
    if (best_path_state == nullptr || isBetterArrival(path_state->get_arrival(), best_path_state->get_arrival(), analysis_type)) {
      best_path_state = path_state;
    }
  }
  return best_path_state;
}

TimingPathState* TimingPropagator::getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type)
{
  if (!hasPathState(timing_point, analysis_type, source_type, trans_type)) {
    return nullptr;
  }

  TimingPathState* best_path_state = nullptr;
  std::map<std::string, TimingPathState>& path_state_map = getPathStateMap(timing_point, analysis_type, source_type, trans_type);
  for (std::pair<const std::string, TimingPathState>& path_state_pair : path_state_map) {
    TimingPathState& path_state = path_state_pair.second;
    if (!isFinite(path_state.get_arrival())) {
      continue;
    }
    if (best_path_state == nullptr || isBetterArrival(path_state.get_arrival(), best_path_state->get_arrival(), analysis_type)) {
      best_path_state = &path_state;
    }
  }
  return best_path_state;
}

TransType TimingPropagator::getEndPointTransType(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type)
{
  TransType best_trans_type = TransType::kNone;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    TimingPathState* path_state = getWorstPathState(timing_point, analysis_type, source_type, trans_type);
    if (path_state == nullptr) {
      continue;
    }
    if (best_trans_type == TransType::kNone
        || isBetterArrival(path_state->get_arrival(), getWorstPathState(timing_point, analysis_type, source_type, best_trans_type)->get_arrival(),
                           analysis_type)) {
      best_trans_type = trans_type;
    }
  }
  return best_trans_type;
}

bool TimingPropagator::isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type)
{
  if (!isFinite(current_arrival)) {
    return true;
  }
  if (analysis_type == AnalysisType::kMin) {
    return candidate_arrival < current_arrival - PW_ERROR;
  }
  return candidate_arrival > current_arrival + PW_ERROR;
}

bool TimingPropagator::isFinite(double value)
{
  return std::isfinite(value);
}

}  // namespace ipw
