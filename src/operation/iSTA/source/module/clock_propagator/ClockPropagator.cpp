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
#include "TimingCaseAnalysis.hpp"

#include <functional>

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
  TimingCaseAnalysis::apply(STADM.getDatabase());
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
  auto& clock_map = database.get_timing_constraint().get_clock_map();
  std::set<std::string> visiting;
  std::set<std::string> visited;
  std::function<void(const std::string&)> append_clock = [&](const std::string& name) {
    if (visited.contains(name)) return;
    if (visiting.contains(name)) {
      STALOG.warn(Loc::current(), "generated clock dependency cycle at '", name, "'");
      return;
    }
    auto clock_iter = clock_map.find(name);
    if (clock_iter == clock_map.end()) return;
    visiting.insert(name);
    const TimingClock& clock = clock_iter->second;
    if (clock.get_is_generated()) append_clock(clock.get_master_clock_name());
    visiting.erase(name);
    visited.insert(name);
    cp_model.clock_list.emplace_back(name, clock.get_source_list(), clock.get_is_propagated());
  };
  for (const auto& [name, clock] : clock_map) append_clock(name);
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
    timing_pair.second.clear_clock_state_map();
    timing_pair.second.get_clock_arrival_map().clear();
    timing_pair.second.get_clock_slew_map().clear();
    timing_pair.second.get_physical_clock_arrival_map().clear();
    timing_pair.second.get_physical_clock_slew_map().clear();
    timing_pair.second.get_path_state_map().clear();
    timing_pair.second.get_data_slew_map().clear();
    timing_pair.second.get_clock_predecessor_map().clear();
    timing_pair.second.get_clock_predecessor_arc_delay_map().clear();
    timing_pair.second.get_clock_predecessor_trans_type_map().clear();
    timing_pair.second.get_physical_clock_predecessor_map().clear();
    timing_pair.second.get_physical_clock_predecessor_arc_delay_map().clear();
    timing_pair.second.get_physical_clock_predecessor_trans_type_map().clear();
    timing_pair.second.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
    timing_pair.second.set_is_clock_point(false);
  }
}

void ClockPropagator::markClockPointList(CPModel& cp_model)
{
  for (auto& clock : cp_model.clock_list) {
    markClockPoint(clock);
  }
}

void ClockPropagator::markClockPoint(CPClock& clock)
{
  Database& database = STADM.getDatabase();
  std::queue<std::string> pin_queue;
  for (const auto& clock_source : clock.get_source_list()) {
    if (!database.get_timing_point_map().contains(clock_source)) {
      STALOG.warn(Loc::current(), "clock '", clock.get_clock_name(), "' has no source");
      continue;
    }
    pin_queue.push(clock_source);
  }

  while (!pin_queue.empty()) {
    std::string pin_name = pin_queue.front();
    pin_queue.pop();

    Pin& pin = database.get_pin_map().at(pin_name);
    // Multiple exported clock modes have no internal data fanout to analyze.
    if (pin.get_is_port() && pin.get_direction() == PinDirection::kOutput && database.get_outgoing_arc_list_map()[pin_name].empty()) {
      continue;
    }
    bool another_root = false;
    for (auto& [name, definition] : database.get_timing_constraint().get_clock_map()) {
      if (name != clock.get_clock_name()
          && std::find(definition.get_source_list().begin(), definition.get_source_list().end(), pin_name) != definition.get_source_list().end()
          && std::find(clock.get_source_list().begin(), clock.get_source_list().end(), pin_name) == clock.get_source_list().end()) {
        another_root = true;
        break;
      }
    }
    if (another_root) {
      continue;
    }
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    if (timing_point.has_clock_state(clock.get_clock_name())) {
      continue;
    }

    timing_point.set_is_clock_point(true);
    timing_point.get_clock_state(clock.get_clock_name());
    if (timing_point.get_clock_name().empty()) timing_point.set_clock_name(clock.get_clock_name());
    clock.add_clock_pin(pin_name);

    if (shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (const auto& arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc)) {
        continue;
      }
      pin_queue.push(arc.get_sink_pin());
    }
  }
}

void ClockPropagator::propagateClockArrival(CPModel& cp_model)
{
  Database& database = STADM.getDatabase();
  for (CPClock& clock : cp_model.clock_list) {
    seedPhysicalClockState(clock);
    propagateClockSlewDelay(clock);
    for (std::string& pin_name : database.get_timing_order_list()) {
      if (TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
          !timing_point.has_clock_state(clock.get_clock_name()) || shouldStopClockPropagation(pin_name)) {
        continue;
      }
      for (const auto& arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
        if (isDisableArc(database.get_arc_list()[arc_idx])) {
          continue;
        }
        propagateClockArrivalArc(clock, arc_idx, AnalysisType::kMax);
        propagateClockArrivalArc(clock, arc_idx, AnalysisType::kMin);
      }
    }
    updateEffectiveClockState(clock);
  }
}

void ClockPropagator::seedPhysicalClockState(CPClock& clock)
{
  Database& database = STADM.getDatabase();
  const TimingClock& definition = database.get_timing_constraint().get_clock_map().at(std::string(clock.get_clock_name()));
  if (definition.get_is_generated() && clock.get_is_propagated() && seedGeneratedClockState(clock, definition)) {
    return;
  }
  if (definition.get_is_generated() && clock.get_is_propagated()) {
    STALOG.warn(Loc::current(), "generated clock '", clock.get_clock_name(), "' has no timing path from master clock '",
                 definition.get_master_clock_name(), "' to its target; using zero insertion delay");
  }
  for (const auto& pin_name : clock.get_source_list()) {
    if (!database.get_timing_point_map().contains(pin_name)) {
      continue;
    }
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    TimingClockPointState& state = timing_point.get_clock_state(clock.get_clock_name());
    state.physical_arrival_map[AnalysisType::kMax][TransType::kRise] = 0.0;
    state.physical_arrival_map[AnalysisType::kMax][TransType::kFall] = 0.0;
    state.physical_arrival_map[AnalysisType::kMin][TransType::kRise] = 0.0;
    state.physical_arrival_map[AnalysisType::kMin][TransType::kFall] = 0.0;
    state.physical_slew_map[AnalysisType::kMax][TransType::kRise] = 0.0;
    state.physical_slew_map[AnalysisType::kMax][TransType::kFall] = 0.0;
    state.physical_slew_map[AnalysisType::kMin][TransType::kRise] = 0.0;
    state.physical_slew_map[AnalysisType::kMin][TransType::kFall] = 0.0;
  }
}

bool ClockPropagator::seedGeneratedClockState(CPClock& clock, const TimingClock& definition)
{
  Database& database = STADM.getDatabase();
  const std::string& master_name = definition.get_master_clock_name();
  const std::string& master_source = definition.get_master_source();
  const bool combinational = definition.get_generated_clock_definition().has_value()
                             && definition.get_generated_clock_definition()->combinational;
  std::set<std::string> source_path_points;
  std::queue<std::string> pin_queue;
  for (const std::string& target : clock.get_source_list()) {
    if (database.get_timing_point_map().contains(target)) pin_queue.push(target);
  }

  bool found_master = false;
  while (!pin_queue.empty()) {
    std::string pin_name = pin_queue.front();
    pin_queue.pop();
    if (!source_path_points.insert(pin_name).second) continue;
    TimingPoint& point = database.get_timing_point_map().at(pin_name);
    const bool is_source_boundary = master_source.empty() ? hasPhysicalClockState(point, master_name) : pin_name == master_source;
    if (is_source_boundary && hasPhysicalClockState(point, master_name)) {
      found_master = true;
      continue;
    }
    for (std::size_t arc_idx : database.get_incoming_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc) || (combinational && arc.get_is_clock_arc())) continue;
      pin_queue.push(arc.get_source_pin());
    }
  }
  if (!found_master) return false;

  for (const std::string& pin_name : source_path_points) {
    TimingPoint& point = database.get_timing_point_map().at(pin_name);
    TimingClockPointState& generated_state = point.get_clock_state(clock.get_clock_name());
    const bool is_source_boundary = master_source.empty() ? hasPhysicalClockState(point, master_name) : pin_name == master_source;
    if (const TimingClockPointState* master_state = point.find_clock_state(master_name);
        is_source_boundary && master_state != nullptr && hasPhysicalClockState(point, master_name)) {
      generated_state.physical_arrival_map = master_state->physical_arrival_map;
      generated_state.physical_slew_map = master_state->physical_slew_map;
    }
  }

  // Timing order normally resolves the source path in one pass. Repeating permits
  // clock-to-Q paths that cross a graph levelization boundary.
  for (std::size_t pass = 0; pass < source_path_points.size(); ++pass) {
    for (const std::string& source_pin : source_path_points) {
      TimingPoint& source_point = database.get_timing_point_map().at(source_pin);
      if (!hasPhysicalClockState(source_point, clock.get_clock_name())) continue;
      for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[source_pin]) {
        Arc& arc = database.get_arc_list()[arc_idx];
        if (!source_path_points.contains(arc.get_sink_pin()) || isDisableArc(arc) || (combinational && arc.get_is_clock_arc())) continue;
        propagateClockSlewDelayArc(clock, arc_idx, AnalysisType::kMax);
        propagateClockSlewDelayArc(clock, arc_idx, AnalysisType::kMin);
        propagateClockArrivalArc(clock, arc_idx, AnalysisType::kMax);
        propagateClockArrivalArc(clock, arc_idx, AnalysisType::kMin);
      }
    }
  }

  return std::ranges::all_of(clock.get_source_list(), [&](const std::string& target) {
    return database.get_timing_point_map().contains(target)
           && hasPhysicalClockState(database.get_timing_point_map().at(target), clock.get_clock_name());
  });
}

bool ClockPropagator::hasPhysicalClockState(const TimingPoint& timing_point, std::string_view clock_name)
{
  const TimingClockPointState* state = timing_point.find_clock_state(clock_name);
  if (state == nullptr) return false;
  for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
    const auto analysis_iter = state->physical_arrival_map.find(analysis_type);
    if (analysis_iter != state->physical_arrival_map.end() && !analysis_iter->second.empty()) return true;
  }
  return false;
}

void ClockPropagator::updateEffectiveClockState(CPClock& clock)
{
  Database& database = STADM.getDatabase();
  TimingClock& definition = database.get_timing_constraint().get_clock_map().at(std::string(clock.get_clock_name()));
  for (const auto& pin_name : clock.get_clock_point_list()) {
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    TimingClockPointState& state = timing_point.get_clock_state(clock.get_clock_name());
    if (clock.get_is_propagated()) {
      state.arrival_map = state.physical_arrival_map;
      for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
        for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
          state.arrival_map[analysis_type][trans_type]
              += definition.get_source_latency(analysis_type, trans_type);
        }
      }
      state.slew_map = state.physical_slew_map;
      state.predecessor_map = state.physical_predecessor_map;
      state.predecessor_arc_delay_map = state.physical_predecessor_arc_delay_map;
      state.predecessor_trans_type_map = state.physical_predecessor_trans_type_map;
      if (timing_point.get_clock_name() == clock.get_clock_name()) {
        timing_point.set_clock_arrival_map(state.arrival_map);
        timing_point.set_clock_slew_map(state.slew_map);
        timing_point.set_clock_predecessor_map(state.predecessor_map);
        timing_point.set_clock_predecessor_arc_delay_map(state.predecessor_arc_delay_map);
        timing_point.set_clock_predecessor_trans_type_map(state.predecessor_trans_type_map);
      }
      continue;
    }

    for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
      for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
        state.arrival_map[analysis_type][trans_type] = definition.get_source_latency(analysis_type, trans_type)
                                                       + definition.get_network_latency(analysis_type, trans_type);
        const auto mode = definition.get_transition_map().find(analysis_type);
        state.slew_map[analysis_type][trans_type]
            = mode != definition.get_transition_map().end() && mode->second.contains(trans_type) ? mode->second.at(trans_type) : 0.0;
      }
    }
    if (timing_point.get_clock_name() == clock.get_clock_name()) {
      timing_point.set_clock_arrival_map(state.arrival_map);
      timing_point.set_clock_slew_map(state.slew_map);
    }
  }
}

void ClockPropagator::propagateClockSlewDelay(CPClock& clock)
{
  Database& database = STADM.getDatabase();
  for (std::string& pin_name : database.get_timing_order_list()) {
    TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
    if (!timing_point.has_clock_state(clock.get_clock_name()) || shouldStopClockPropagation(pin_name)) {
      continue;
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      if (isDisableArc(database.get_arc_list()[arc_idx])) {
        continue;
      }
      propagateClockSlewDelayArc(clock, arc_idx, AnalysisType::kMax);
      propagateClockSlewDelayArc(clock, arc_idx, AnalysisType::kMin);
    }
  }
}

void ClockPropagator::propagateClockSlewDelayArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type)
{
  propagateClockSlewDelayArc(clock, arc_idx, analysis_type, TransType::kRise);
  propagateClockSlewDelayArc(clock, arc_idx, analysis_type, TransType::kFall);
}

void ClockPropagator::propagateClockSlewDelayArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = STADM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  if (!source_point.has_clock_state(clock.get_clock_name()) || !sink_point.has_clock_state(clock.get_clock_name())) {
    return;
  }
  TimingClockPointState& source_state = source_point.get_clock_state(clock.get_clock_name());
  if (!source_state.physical_slew_map.contains(analysis_type) || !source_state.physical_slew_map[analysis_type].contains(input_trans_type)) {
    return;
  }
  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    updateClockSlewDelay(clock.get_clock_name(), arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void ClockPropagator::updateClockSlewDelay(std::string_view clock_name, Arc& arc, TimingPoint& source_point, TimingPoint& sink_point,
                                           AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  TimingClockPointState& source_state = source_point.get_clock_state(clock_name);
  TimingClockPointState& sink_state = sink_point.get_clock_state(clock_name);
  double input_slew = source_state.physical_slew_map[analysis_type][input_trans_type];
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
  if (!sink_state.physical_slew_map.contains(analysis_type) || !sink_state.physical_slew_map[analysis_type].contains(output_trans_type)
      || isBetterSlew(output_slew, sink_state.physical_slew_map[analysis_type][output_trans_type], analysis_type)) {
    sink_state.physical_slew_map[analysis_type][output_trans_type] = output_slew;
  }
}

void ClockPropagator::propagateClockArrivalArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type)
{
  propagateClockArrivalArc(clock, arc_idx, analysis_type, TransType::kRise);
  propagateClockArrivalArc(clock, arc_idx, analysis_type, TransType::kFall);
}

void ClockPropagator::propagateClockArrivalArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type)
{
  Database& database = STADM.getDatabase();
  Arc& arc = database.get_arc_list()[arc_idx];
  if (isDisableArc(arc)) {
    return;
  }
  TimingPoint& source_point = database.get_timing_point_map()[arc.get_source_pin()];
  TimingPoint& sink_point = database.get_timing_point_map()[arc.get_sink_pin()];
  if (!source_point.has_clock_state(clock.get_clock_name()) || !sink_point.has_clock_state(clock.get_clock_name())) {
    return;
  }
  if (!hasClockArrival(source_point, clock.get_clock_name(), analysis_type, input_trans_type)) {
    return;
  }
  TimingClockPointState& source_state = source_point.get_clock_state(clock.get_clock_name());
  if (!source_state.physical_slew_map.contains(analysis_type) || !source_state.physical_slew_map[analysis_type].contains(input_trans_type)) {
    return;
  }

  for (TransType output_trans_type : getOutputTransTypeList(arc, analysis_type, input_trans_type)) {
    updateClockPathState(clock.get_clock_name(), arc, source_point, sink_point, analysis_type, input_trans_type, output_trans_type);
  }
}

void ClockPropagator::updateClockPathState(std::string_view clock_name, Arc& arc, TimingPoint& source_point, TimingPoint& sink_point,
                                           AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type)
{
  double arc_delay = getArcDelay(arc, analysis_type, input_trans_type, output_trans_type);
  double candidate_arrival = roundTime(getClockArrival(source_point, clock_name, analysis_type, input_trans_type) + arc_delay);
  if (!hasClockArrival(sink_point, clock_name, analysis_type, output_trans_type)
      || isBetterArrival(candidate_arrival, getClockArrival(sink_point, clock_name, analysis_type, output_trans_type), analysis_type)) {
    updateClockArrival(sink_point, clock_name, analysis_type, output_trans_type, candidate_arrival);
    updateClockPredecessor(sink_point, clock_name, analysis_type, output_trans_type, input_trans_type, arc, arc_delay);
  }
}

bool ClockPropagator::hasClockArrival(const TimingPoint& timing_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  const TimingClockPointState* state = timing_point.find_clock_state(clock_name);
  return state != nullptr && state->physical_arrival_map.contains(analysis_type) && state->physical_arrival_map.at(analysis_type).contains(trans_type);
}

double ClockPropagator::getClockArrival(const TimingPoint& timing_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type)
{
  if (!hasClockArrival(timing_point, clock_name, analysis_type, trans_type)) {
    return 0.0;
  }
  return timing_point.find_clock_state(clock_name)->physical_arrival_map.at(analysis_type).at(trans_type);
}

void ClockPropagator::updateClockArrival(TimingPoint& timing_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                         double clock_arrival)
{
  timing_point.get_clock_state(clock_name).physical_arrival_map[analysis_type][trans_type] = clock_arrival;
}

void ClockPropagator::updateClockPredecessor(TimingPoint& timing_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                             TransType predecessor_trans_type, Arc& arc, double arc_delay)
{
  TimingClockPointState& state = timing_point.get_clock_state(clock_name);
  state.physical_predecessor_map[analysis_type][trans_type] = arc.get_source_pin();
  state.physical_predecessor_arc_delay_map[analysis_type][trans_type] = arc_delay;
  state.physical_predecessor_trans_type_map[analysis_type][trans_type] = predecessor_trans_type;
}

bool ClockPropagator::shouldStopClockPropagation(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || !database.get_instance_map().contains(pin.get_instance_name())) {
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
