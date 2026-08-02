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

namespace ista {

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
    STALOG.error(Loc::current(), "The instance not initialized!");
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
  STALOG.info(Loc::current(), "Starting...");

  Database& database = STADM.getDatabase();
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

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
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

bool TimingPropagator::isSequentialClockPin(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_clock_pin_name();
}

bool TimingPropagator::hasIncomingPhysicalSlewArc(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
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
  tp_model.set_start_point_list(STADM.getDatabase().get_start_point_list());
}

double TimingPropagator::getClockArrival(std::string& pin_name, AnalysisType analysis_type)
{
  return getClockArrival(pin_name, analysis_type, TransType::kRise);
}

double TimingPropagator::getClockArrival(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  return getClockArrival(timing_point, analysis_type, trans_type);
}

double TimingPropagator::getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (timing_point.get_clock_arrival_map().count(analysis_type) == 0
      || timing_point.get_clock_arrival_map()[analysis_type].count(trans_type) == 0) {
    return 0.0;
  }
  return timing_point.get_clock_arrival_map()[analysis_type][trans_type];
}

double TimingPropagator::getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  if (timing_point.get_clock_slew_map().count(analysis_type) == 0 || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0) {
    return 0.0;
  }
  return timing_point.get_clock_slew_map()[analysis_type][trans_type];
}

void TimingPropagator::seedStartPointList(TPModel& tp_model)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
  TimingPoint& timing_point = database.get_timing_point_map()[start_point];
  if (isSequentialClockPin(start_point)
      && hasIncomingPhysicalSlewArc(start_point)
      && (timing_point.get_clock_slew_map().count(analysis_type) == 0
          || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0)) {
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
  Database& database = STADM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  // Slew must reach a sequential CK for C2Q delay and timing checks.  Arrival
  // and path-state propagation still stop at this boundary.
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  if (!hasDataSlew(source_point, analysis_type, input_trans_type)) {
    return;
  }
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    updateDataSlewDelay(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void TimingPropagator::updateDataSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                                           TransType input_trans_type, TransType output_trans_type)
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
  STADC.calculate(dc_task);
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
    return candidate_delay < current_delay - STA_ERROR;
  }
  return candidate_delay > current_delay + STA_ERROR;
}

bool TimingPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - STA_ERROR;
  }
  return candidate_slew > current_slew + STA_ERROR;
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
  Database& database = STADM.getDatabase();
  if (isClockSourceStartPoint(start_point)) {
    return getStartPointClockEdge(start_point, analysis_type, trans_type);
  }
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    if (analysis_type == AnalysisType::kMin && port_constraint_map.count(start_point) > 0 && port_constraint_map[start_point].get_has_input_delay_min()) {
      return port_constraint_map[start_point].get_input_delay_min();
    }
    if (port_constraint_map.count(start_point) > 0 && port_constraint_map[start_point].get_has_input_delay_max()) {
      return port_constraint_map[start_point].get_input_delay_max();
    }
    return 0.0;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    return getClockArrival(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
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
    STADC.calculate(dc_task);
    if (dc_task.get_is_valid()) {
      return getClockArrival(instance.get_clock_pin_name(), analysis_type, clock_trans_type) + dc_task.get_timing_result().get_delay();
    }
  }
  return 0.0;
}

bool TimingPropagator::isClockSourceStartPoint(std::string& start_point)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  return pin.get_is_port() && getStartPointClock(start_point) != nullptr;
}

TimingClock* TimingPropagator::getStartPointClock(std::string& start_point)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    if (port_constraint_map.count(start_point) > 0 && port_constraint_map[start_point].get_has_input_transition()) {
      return port_constraint_map[start_point].get_input_transition();
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
    STADC.calculate(dc_task);
    if (dc_task.get_is_valid()) {
      return dc_task.get_timing_result().get_slew();
    }
  }
  return 0.0;
}


double TimingPropagator::getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type)
{
  return getStartPointLaunchTime(start_point, analysis_type, TransType::kRise);
}

double TimingPropagator::getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  if (isClockSourceStartPoint(start_point)) {
    return getStartPointClockEdge(start_point, analysis_type, trans_type);
  }
  Pin& pin = database.get_pin_map()[start_point];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return 0.0;
  }
  return getClockArrival(instance.get_clock_pin_name(), analysis_type, getClockTransType(instance.get_clock_to_q_arc()));
}

std::string TimingPropagator::getStartPointCrprClockPin(std::string& start_point)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
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

std::string TimingPropagator::getClockName(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  TimingClock* timing_clock = getStartPointClock(pin_name);
  if (timing_clock != nullptr) {
    return timing_clock->get_clock_name();
  }
  Pin& pin = database.get_pin_map()[pin_name];
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

std::string TimingPropagator::getPathStateStartPoint(std::string& start_point)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
  PathSourceType source_type = getStartPointSourceType(start_point, analysis_type);
  if (source_type == PathSourceType::kNone) {
    return;
  }
  std::string path_state_start_point = getPathStateStartPoint(start_point);
  TimingPathState& rise_path_state
      = database.get_timing_point_map()[start_point].get_path_state_map()[analysis_type][source_type][TransType::kRise][path_state_start_point];
  rise_path_state.set_arrival(getStartPointArrival(start_point, analysis_type, TransType::kRise));
  rise_path_state.set_slew(getStartPointSlew(start_point, analysis_type, TransType::kRise));
  rise_path_state.set_launch_time(getStartPointLaunchTime(start_point, analysis_type, TransType::kRise));
  rise_path_state.set_start_point(path_state_start_point);
  rise_path_state.set_clock_name(getClockName(start_point));
  rise_path_state.set_crpr_clock_pin(getStartPointCrprClockPin(start_point));
  rise_path_state.get_predecessor().clear();
  rise_path_state.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
  rise_path_state.set_predecessor_arc_delay(0.0);
  rise_path_state.set_trans_type(TransType::kRise);
  rise_path_state.set_predecessor_trans_type(TransType::kNone);
  rise_path_state.set_crpr_clock_trans_type(getStartPointCrprClockTransType(start_point));
  TimingPathState& fall_path_state
      = database.get_timing_point_map()[start_point].get_path_state_map()[analysis_type][source_type][TransType::kFall][path_state_start_point];
  fall_path_state.set_arrival(getStartPointArrival(start_point, analysis_type, TransType::kFall));
  fall_path_state.set_slew(getStartPointSlew(start_point, analysis_type, TransType::kFall));
  fall_path_state.set_launch_time(getStartPointLaunchTime(start_point, analysis_type, TransType::kFall));
  fall_path_state.set_start_point(path_state_start_point);
  fall_path_state.set_clock_name(getClockName(start_point));
  fall_path_state.set_crpr_clock_pin(getStartPointCrprClockPin(start_point));
  fall_path_state.get_predecessor().clear();
  fall_path_state.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
  fall_path_state.set_predecessor_arc_delay(0.0);
  fall_path_state.set_trans_type(TransType::kFall);
  fall_path_state.set_predecessor_trans_type(TransType::kNone);
  fall_path_state.set_crpr_clock_trans_type(getStartPointCrprClockTransType(start_point));
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
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[start_point];
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

bool TimingPropagator::isRegisterStartPoint(std::string& start_point)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
  return database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point();
}

void TimingPropagator::propagateArrivalArc(std::size_t arc_idx)
{
  Database& database = STADM.getDatabase();
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

void TimingPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type,
                                             TransType input_trans_type)
{
  Database& database = STADM.getDatabase();
  if (isDisableArc(database.get_arc_list()[arc_idx])) {
    return;
  }
  for (TransType output_trans_type : getOutputTransTypeList(database.get_arc_list()[arc_idx], analysis_type, input_trans_type)) {
    propagatePathStateArc(arc_idx, analysis_type, source_type, input_trans_type, output_trans_type);
  }
}

void TimingPropagator::propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type,
                                             TransType input_trans_type, TransType output_trans_type)
{
  Database& database = STADM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
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
    std::string& start_point = source_path_state.get_start_point();
    std::map<std::string, TimingPathState>& sink_path_state_map = getPathStateMap(sink_point, analysis_type, source_type, output_trans_type);
    if (sink_path_state_map.count(start_point) == 0 || isBetterArrival(candidate_arrival, sink_path_state_map[start_point].get_arrival(), analysis_type)) {
      TimingPathState& sink_path_state = sink_path_state_map[start_point];
      sink_path_state.set_arrival(candidate_arrival);
      sink_path_state.set_slew(getDataSlew(sink_point, analysis_type, output_trans_type));
      sink_path_state.set_start_point(start_point);
      sink_path_state.set_predecessor(arc.get_source_pin());
      sink_path_state.set_predecessor_arc_idx(arc_idx);
      sink_path_state.set_predecessor_arc_delay(arc_delay);
      sink_path_state.set_launch_time(source_path_state.get_launch_time());
      sink_path_state.set_clock_name(source_path_state.get_clock_name());
      sink_path_state.set_crpr_clock_pin(source_path_state.get_crpr_clock_pin());
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
                                                std::string& start_point)
{
  return timing_point.get_path_state_map()[analysis_type][source_type][trans_type][start_point];
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
    return candidate_arrival < current_arrival - STA_ERROR;
  }
  return candidate_arrival > current_arrival + STA_ERROR;
}

bool TimingPropagator::isFinite(double value)
{
  return std::isfinite(value);
}

}  // namespace ista
