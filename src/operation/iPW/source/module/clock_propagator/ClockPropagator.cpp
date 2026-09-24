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
#include "ClockPropagator.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ipw {

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
    PWLOG.error(Loc::current(), "The instance not initialized!");
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
  PWLOG.info(Loc::current(), "Starting...");

  CPModel cp_model;
  initSignalPointList(cp_model);
  std::vector<std::string> clock_name_list;
  buildClockNameList(clock_name_list);
  markClockPointList(cp_model, clock_name_list);
  propagateClockSlew(cp_model, clock_name_list);

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

ClockPropagator* ClockPropagator::_cp_instance = nullptr;

bool ClockPropagator::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

void ClockPropagator::initSignalPointList(CPModel& cp_model)
{
  for (std::pair<const std::string, TimingPoint>& timing_pair : PWDM.getDatabase().get_timing_point_map()) {
    TimingPoint& timing_point = timing_pair.second;
    timing_point.get_clock_slew_map().clear();
    timing_point.get_data_slew_map().clear();
    timing_point.set_is_clock_point(false);
  }
}

void ClockPropagator::buildClockNameList(std::vector<std::string>& clock_name_list)
{
  clock_name_list.clear();
  Database& database = PWDM.getDatabase();
  std::map<std::string, TimingClock>& clock_map = database.get_timing_constraint().get_clock_map();
  std::set<std::string> visiting;
  std::set<std::string> visited;
  std::function<void(const std::string&)> append_clock = [&](const std::string& clock_name) {
    if (visited.contains(clock_name)) {
      return;
    }
    if (visiting.contains(clock_name)) {
      PWLOG.warn(Loc::current(), "generated clock dependency cycle at '", clock_name, "'");
      return;
    }
    if (clock_map.count(clock_name) == 0) {
      return;
    }
    visiting.insert(clock_name);
    TimingClock& timing_clock = clock_map[clock_name];
    if (timing_clock.get_is_generated()) {
      append_clock(timing_clock.get_master_clock_name());
    }
    visiting.erase(clock_name);
    visited.insert(clock_name);
    clock_name_list.emplace_back(clock_name);
  };
  for (std::pair<const std::string, TimingClock>& clock_pair : clock_map) {
    append_clock(clock_pair.first);
  }
}

void ClockPropagator::markClockPointList(CPModel& cp_model, std::vector<std::string>& clock_name_list)
{
  for (std::string& clock_name : clock_name_list) {
    markClockPoint(cp_model, clock_name);
  }
}

void ClockPropagator::markClockPoint(CPModel& cp_model, std::string& clock_name)
{
  Database& database = PWDM.getDatabase();
  TimingClock& timing_clock = database.get_timing_constraint().get_clock_map().at(clock_name);
  std::queue<std::string> pin_queue;
  for (const std::string& clock_source : timing_clock.get_source_list()) {
    if (!database.get_timing_point_map().contains(clock_source)) {
      PWLOG.warn(Loc::current(), "clock '", clock_name, "' has no source");
      continue;
    }
    pin_queue.push(clock_source);
  }

  while (!pin_queue.empty()) {
    std::string pin_name = pin_queue.front();
    pin_queue.pop();
    Pin& pin = database.get_pin_map().at(pin_name);
    if (pin.get_is_port() && pin.get_direction() == PinDirection::kOutput && database.get_outgoing_arc_list_map()[pin_name].empty()) {
      continue;
    }
    bool another_root = false;
    for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
      TimingClock& definition = clock_pair.second;
      if (clock_pair.first != clock_name
          && std::find(definition.get_source_list().begin(), definition.get_source_list().end(), pin_name) != definition.get_source_list().end()
          && std::find(timing_clock.get_source_list().begin(), timing_clock.get_source_list().end(), pin_name) == timing_clock.get_source_list().end()) {
        another_root = true;
        break;
      }
    }
    if (another_root) {
      continue;
    }
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    if (cp_model.has_clock_state(pin_name, clock_name)) {
      continue;
    }
    timing_point.set_is_clock_point(true);
    cp_model.get_clock_state(pin_name, clock_name);
    if (cp_model.get_clock_name(pin_name).empty()) {
      cp_model.set_clock_name(pin_name, clock_name);
    }
    if (shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (!isDisableArc(arc)) {
        pin_queue.push(arc.get_sink_pin());
      }
    }
  }
}

void ClockPropagator::propagateClockSlew(CPModel& cp_model, std::vector<std::string>& clock_name_list)
{
  for (std::string& clock_name : clock_name_list) {
    seedPhysicalClockSlew(cp_model, clock_name);
    propagateClockSlew(cp_model, clock_name);
    updateEffectiveClockSlew(cp_model, clock_name);
  }
}

void ClockPropagator::seedPhysicalClockSlew(CPModel& cp_model, std::string& clock_name)
{
  Database& database = PWDM.getDatabase();
  TimingClock& timing_clock = database.get_timing_constraint().get_clock_map().at(clock_name);
  if (timing_clock.get_is_generated() && timing_clock.get_is_propagated() && seedGeneratedClockSlew(cp_model, clock_name, timing_clock)) {
    return;
  }
  if (timing_clock.get_is_generated() && timing_clock.get_is_propagated()) {
    PWLOG.warn(Loc::current(), "generated clock '", clock_name, "' has no signal path from master clock '",
               timing_clock.get_master_clock_name(), "' to its target; using zero insertion slew");
  }
  for (const std::string& pin_name : timing_clock.get_source_list()) {
    if (!database.get_timing_point_map().contains(pin_name)) {
      continue;
    }
    ClockSlewState& state = cp_model.get_clock_state(pin_name, clock_name);
    state.get_physical_slew_map()[AnalysisType::kMax][TransType::kRise] = 0.0;
    state.get_physical_slew_map()[AnalysisType::kMax][TransType::kFall] = 0.0;
    state.get_physical_slew_map()[AnalysisType::kMin][TransType::kRise] = 0.0;
    state.get_physical_slew_map()[AnalysisType::kMin][TransType::kFall] = 0.0;
  }
}

bool ClockPropagator::seedGeneratedClockSlew(CPModel& cp_model, std::string& clock_name, const TimingClock& timing_clock)
{
  Database& database = PWDM.getDatabase();
  const std::string& master_clock_name = timing_clock.get_master_clock_name();
  const std::string& master_source = timing_clock.get_master_source();
  bool is_combinational = timing_clock.get_is_generated_combinational();
  std::set<std::string> source_path_point_set;
  std::queue<std::string> pin_queue;
  for (const std::string& target : timing_clock.get_source_list()) {
    if (database.get_timing_point_map().contains(target)) {
      pin_queue.push(target);
    }
  }

  bool found_master = false;
  while (!pin_queue.empty()) {
    std::string pin_name = pin_queue.front();
    pin_queue.pop();
    if (!source_path_point_set.insert(pin_name).second) {
      continue;
    }
    bool is_source_boundary = master_source.empty() ? hasPhysicalClockSlew(cp_model, pin_name, master_clock_name) : pin_name == master_source;
    if (is_source_boundary && hasPhysicalClockSlew(cp_model, pin_name, master_clock_name)) {
      found_master = true;
      continue;
    }
    for (std::size_t arc_idx : database.get_incoming_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (!isDisableArc(arc) && !(is_combinational && arc.get_is_clock_arc())) {
        pin_queue.push(arc.get_source_pin());
      }
    }
  }
  if (!found_master) {
    return false;
  }

  for (const std::string& pin_name : source_path_point_set) {
    ClockSlewState& generated_state = cp_model.get_clock_state(pin_name, clock_name);
    bool is_source_boundary = master_source.empty() ? hasPhysicalClockSlew(cp_model, pin_name, master_clock_name) : pin_name == master_source;
    const ClockSlewState* master_state = cp_model.find_clock_state(pin_name, master_clock_name);
    if (is_source_boundary && master_state != nullptr && hasPhysicalClockSlew(cp_model, pin_name, master_clock_name)) {
      generated_state.set_physical_slew_map(master_state->get_physical_slew_map());
    }
  }

  for (std::size_t pass = 0; pass < source_path_point_set.size(); ++pass) {
    for (const std::string& source_pin : source_path_point_set) {
      if (!hasPhysicalClockSlew(cp_model, source_pin, clock_name)) {
        continue;
      }
      for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[source_pin]) {
        Arc& arc = database.get_arc_list()[arc_idx];
        if (source_path_point_set.contains(arc.get_sink_pin()) && !isDisableArc(arc) && !(is_combinational && arc.get_is_clock_arc())) {
          propagateClockSlewArc(cp_model, clock_name, arc_idx, AnalysisType::kMax);
          propagateClockSlewArc(cp_model, clock_name, arc_idx, AnalysisType::kMin);
        }
      }
    }
  }

  return std::ranges::all_of(timing_clock.get_source_list(), [&](const std::string& target) {
    return database.get_timing_point_map().contains(target)
           && hasPhysicalClockSlew(cp_model, target, clock_name);
  });
}

bool ClockPropagator::hasPhysicalClockSlew(const CPModel& cp_model, std::string_view pin_name, std::string_view clock_name)
{
  const ClockSlewState* state = cp_model.find_clock_state(pin_name, clock_name);
  if (state == nullptr) {
    return false;
  }
  for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
    const std::map<AnalysisType, std::map<TransType, double>>& physical_slew_map = state->get_physical_slew_map();
    if (physical_slew_map.contains(analysis_type) && !physical_slew_map.at(analysis_type).empty()) {
      return true;
    }
  }
  return false;
}

void ClockPropagator::updateEffectiveClockSlew(CPModel& cp_model, std::string& clock_name)
{
  Database& database = PWDM.getDatabase();
  TimingClock& timing_clock = database.get_timing_constraint().get_clock_map().at(clock_name);
  for (std::pair<const std::string, TimingPoint>& timing_pair : database.get_timing_point_map()) {
    if (!cp_model.has_clock_state(timing_pair.first, clock_name)) {
      continue;
    }
    TimingPoint& timing_point = timing_pair.second;
    ClockSlewState& state = cp_model.get_clock_state(timing_pair.first, clock_name);
    if (timing_clock.get_is_propagated()) {
      state.set_slew_map(state.get_physical_slew_map());
    } else {
      std::map<AnalysisType, std::map<TransType, double>>& slew_map = state.get_slew_map();
      for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
        for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
          const auto transition_iter = timing_clock.get_transition_map().find(analysis_type);
          slew_map[analysis_type][trans_type] = transition_iter != timing_clock.get_transition_map().end()
                                                   && transition_iter->second.contains(trans_type)
                                               ? transition_iter->second.at(trans_type)
                                               : 0.0;
        }
      }
    }
    if (cp_model.get_clock_name(timing_pair.first) == clock_name) {
      timing_point.set_clock_slew_map(state.get_slew_map());
    }
  }
}

void ClockPropagator::propagateClockSlew(CPModel& cp_model, std::string& clock_name)
{
  Database& database = PWDM.getDatabase();
  for (std::string& pin_name : database.get_signal_order_list()) {
    if (!cp_model.has_clock_state(pin_name, clock_name) || shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (!isDisableArc(database.get_arc_list()[arc_idx])) {
        propagateClockSlewArc(cp_model, clock_name, arc_idx, AnalysisType::kMax);
        propagateClockSlewArc(cp_model, clock_name, arc_idx, AnalysisType::kMin);
      }
    }
  }
}

void ClockPropagator::propagateClockSlewArc(CPModel& cp_model, std::string& clock_name, std::size_t arc_idx, AnalysisType analysis_type)
{
  propagateClockSlewArc(cp_model, clock_name, arc_idx, analysis_type, TransType::kRise);
  propagateClockSlewArc(cp_model, clock_name, arc_idx, analysis_type, TransType::kFall);
}

void ClockPropagator::propagateClockSlewArc(CPModel& cp_model, std::string& clock_name, std::size_t arc_idx, AnalysisType analysis_type,
                                             TransType input_trans_type)
{
  Database& database = PWDM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (!cp_model.has_clock_state(arc.get_source_pin(), clock_name) || !cp_model.has_clock_state(arc.get_sink_pin(), clock_name)) {
    return;
  }
  ClockSlewState& source_state = cp_model.get_clock_state(arc.get_source_pin(), clock_name);
  if (!source_state.get_physical_slew_map().contains(analysis_type)
      || !source_state.get_physical_slew_map()[analysis_type].contains(input_trans_type)) {
    return;
  }
  for (TransType output_trans_type : getOutputTransTypeList(arc, input_trans_type)) {
    updateClockSlew(cp_model, clock_name, arc, analysis_type, input_trans_type, output_trans_type);
  }
}

void ClockPropagator::updateClockSlew(CPModel& cp_model, std::string_view clock_name, Arc& arc, AnalysisType analysis_type,
                                      TransType input_trans_type, TransType output_trans_type)
{
  ClockSlewState& source_state = cp_model.get_clock_state(arc.get_source_pin(), clock_name);
  ClockSlewState& sink_state = cp_model.get_clock_state(arc.get_sink_pin(), clock_name);
  DCTask dc_task;
  dc_task.set_arc(&arc);
  dc_task.set_analysis_type(analysis_type);
  dc_task.set_input_trans_type(input_trans_type);
  dc_task.set_output_trans_type(output_trans_type);
  dc_task.set_input_slew(source_state.get_physical_slew_map()[analysis_type][input_trans_type]);
  PWDC.calculate(dc_task);
  if (!dc_task.get_is_valid()) {
    return;
  }
  double output_slew = dc_task.get_output_slew();
  if (!sink_state.get_physical_slew_map().contains(analysis_type)
      || !sink_state.get_physical_slew_map()[analysis_type].contains(output_trans_type)
      || isBetterSlew(output_slew, sink_state.get_physical_slew_map()[analysis_type][output_trans_type], analysis_type)) {
    sink_state.get_physical_slew_map()[analysis_type][output_trans_type] = output_slew;
  }
}

bool ClockPropagator::shouldStopClockPropagation(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || !database.get_instance_map().contains(pin.get_instance_name())) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

bool ClockPropagator::isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return candidate_slew < current_slew - PW_ERROR;
  }
  return candidate_slew > current_slew + PW_ERROR;
}

std::vector<TransType> ClockPropagator::getOutputTransTypeList(Arc& arc, TransType input_trans_type)
{
  return PWDC.getOutputTransTypeList(arc, input_trans_type);
}

}  // namespace ipw
