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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "SignalPropagator.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ipw {

// public

void SignalPropagator::initInst()
{
  if (_sp_instance == nullptr) {
    _sp_instance = new SignalPropagator();
  }
}

SignalPropagator& SignalPropagator::getInst()
{
  if (_sp_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_sp_instance;
}

void SignalPropagator::destroyInst()
{
  if (_sp_instance != nullptr) {
    delete _sp_instance;
    _sp_instance = nullptr;
  }
}

// function

void SignalPropagator::propagate()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  seedSignalSlewList();
  propagateSignalSlew();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

SignalPropagator* SignalPropagator::_sp_instance = nullptr;

bool SignalPropagator::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

bool SignalPropagator::shouldStopSignalSlewPropagation(Arc& arc)
{
  if (arc.get_type() != ArcType::kNet || !isSequentialClockPin(arc.get_sink_pin())) {
    return false;
  }
  return PWDM.getDatabase().get_timing_point_map()[arc.get_sink_pin()].get_is_clock_point();
}

bool SignalPropagator::isSequentialClockPin(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_clock_pin_name();
}

bool SignalPropagator::hasIncomingPhysicalSlewArc(std::string& pin_name)
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

void SignalPropagator::seedSignalSlewList()
{
  for (std::string& source_pin : PWDM.getDatabase().get_source_pin_list()) {
    seedSignalSlew(source_pin, AnalysisType::kMax);
    seedSignalSlew(source_pin, AnalysisType::kMin);
  }
}

void SignalPropagator::seedSignalSlew(std::string& source_pin, AnalysisType analysis_type)
{
  seedSignalSlew(source_pin, analysis_type, TransType::kRise);
  seedSignalSlew(source_pin, analysis_type, TransType::kFall);
}

void SignalPropagator::seedSignalSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  if (!database.get_timing_constraint().allowsTransition(source_pin, trans_type)) {
    return;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[source_pin];
  if (isSequentialClockPin(source_pin) && hasIncomingPhysicalSlewArc(source_pin)
      && (timing_point.get_clock_slew_map().count(analysis_type) == 0 || timing_point.get_clock_slew_map()[analysis_type].count(trans_type) == 0)) {
    return;
  }
  timing_point.get_data_slew_map()[analysis_type][trans_type] = getSourcePinSlew(source_pin, analysis_type, trans_type);
}

double SignalPropagator::getSourcePinSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[source_pin];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    if (port_constraint_map.count(source_pin) == 0) {
      return 0.0;
    }
    TimingPortConstraint& port_constraint = port_constraint_map[source_pin];
    if (port_constraint.get_has_driving_cell(analysis_type, trans_type)) {
      std::optional<double> driving_cell_slew = getDrivingCellSlew(source_pin, analysis_type, trans_type);
      return driving_cell_slew ? *driving_cell_slew : 0.0;
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
  if (instance.get_is_sequential() && source_pin == instance.get_clock_pin_name()) {
    TimingCellArc* clock_to_q_arc = instance.get_clock_to_q_arc();
    TransType clock_trans_type = clock_to_q_arc == nullptr ? TransType::kRise : getClockTransType(*clock_to_q_arc);
    return getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
  }
  if (instance.get_is_sequential() && source_pin == instance.get_output_pin_name()) {
    TimingCellArc* clock_to_q_arc = instance.get_clock_to_q_arc();
    if (clock_to_q_arc == nullptr) {
      return 0.0;
    }
    TransType clock_trans_type = getClockTransType(*clock_to_q_arc);
    DCTask dc_task;
    dc_task.set_timing_cell_arc(clock_to_q_arc);
    dc_task.set_output_pin(source_pin);
    dc_task.set_analysis_type(analysis_type);
    dc_task.set_input_trans_type(clock_trans_type);
    dc_task.set_output_trans_type(trans_type);
    dc_task.set_input_slew(getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type));
    PWDC.calculate(dc_task);
    if (dc_task.get_is_valid()) {
      return dc_task.get_output_slew();
    }
  }
  return 0.0;
}

std::optional<double> SignalPropagator::getDrivingCellSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
  if (!port_constraint_map.contains(source_pin)) {
    return std::nullopt;
  }
  TimingDrivingCell* driving_cell = port_constraint_map.at(source_pin).get_driving_cell(analysis_type, trans_type);
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
    double output_slew = 0.0;
    if (PWDC.calculateDrivingCell(source_pin, timing_cell_arc, analysis_type, trans_type, driving_cell->get_input_transition_rise(),
                                   driving_cell->get_input_transition_fall(), output_slew)) {
      return output_slew;
    }
  }
  return std::nullopt;
}

TransType SignalPropagator::getClockTransType(TimingCellArc& timing_cell_arc)
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

double SignalPropagator::getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
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

void SignalPropagator::propagateSignalSlew()
{
  Database& database = PWDM.getDatabase();
  for (std::string& pin_name : database.get_signal_order_list()) {
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (isDisableArc(database.get_arc_list()[arc_idx])) {
        continue;
      }
      propagateSignalSlewArc(arc_idx);
    }
  }
}

void SignalPropagator::propagateSignalSlewArc(std::size_t arc_idx)
{
  propagateSignalSlewArc(arc_idx, AnalysisType::kMax, TransType::kRise);
  propagateSignalSlewArc(arc_idx, AnalysisType::kMax, TransType::kFall);
  propagateSignalSlewArc(arc_idx, AnalysisType::kMin, TransType::kRise);
  propagateSignalSlewArc(arc_idx, AnalysisType::kMin, TransType::kFall);
}

void SignalPropagator::propagateSignalSlewArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = PWDM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc) || shouldStopSignalSlewPropagation(arc)
      || !database.get_timing_constraint().allowsTransition(arc.get_source_pin(), input_trans_type)) {
    return;
  }
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  if (!hasSignalSlew(source_point, analysis_type, input_trans_type)) {
    return;
  }
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  for (TransType output_trans_type : getOutputTransTypeList(arc, input_trans_type)) {
    if (!database.get_timing_constraint().allowsTransition(arc.get_sink_pin(), output_trans_type)) {
      continue;
    }
    updateSignalSlew(arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void SignalPropagator::updateSignalSlew(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                                        TransType input_trans_type, TransType output_trans_type)
{
  DCTask dc_task;
  dc_task.set_arc(&arc);
  dc_task.set_analysis_type(analysis_type);
  dc_task.set_input_trans_type(input_trans_type);
  dc_task.set_output_trans_type(output_trans_type);
  dc_task.set_input_slew(getSignalSlew(source_point, analysis_type, input_trans_type));
  if (isSequentialClockPin(arc.get_source_pin())) {
    dc_task.set_output_slew_input_slew(getClockSlew(arc.get_source_pin(), analysis_type, input_trans_type));
  }
  PWDC.calculate(dc_task);
  if (!dc_task.get_is_valid()) {
    return;
  }
  updateSignalSlew(sink_point, analysis_type, output_trans_type, dc_task.get_output_slew());
}

void SignalPropagator::updateSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double signal_slew)
{
  if (!hasSignalSlew(timing_point, analysis_type, trans_type)
      || isBetterSlew(signal_slew, timing_point.get_data_slew_map()[analysis_type][trans_type], analysis_type)) {
    timing_point.get_data_slew_map()[analysis_type][trans_type] = signal_slew;
  }
}

bool SignalPropagator::hasSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  return timing_point.get_data_slew_map().count(analysis_type) > 0 && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0;
}

double SignalPropagator::getSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type)
{
  if (!hasSignalSlew(timing_point, analysis_type, trans_type)) {
    return 0.0;
  }
  return timing_point.get_data_slew_map()[analysis_type][trans_type];
}

bool SignalPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - PW_ERROR;
  }
  return candidate_slew > current_slew + PW_ERROR;
}

std::vector<TransType> SignalPropagator::getOutputTransTypeList(Arc& arc, TransType input_trans_type)
{
  return PWDC.getOutputTransTypeList(arc, input_trans_type);
}

}  // namespace ipw
