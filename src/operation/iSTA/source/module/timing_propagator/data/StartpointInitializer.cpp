#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sdc/ClockEdges.hpp"
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
#include "DelayCalculator.hpp"
#include "StartpointInitializer.hpp"
#include "TransitionFilter.hpp"
#include "Utility.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
void StartpointInitializer::seed(std::vector<std::string>& startpoints)
{
  Database& database = _database;
  _through_index.rebuild(database.sdc(), database.network());
  for (std::string& start_point : startpoints) {
    TimingPoint& timing_point = database.graph().driverPoint(start_point);
    timing_point.set_arrival(getStartPointArrival(start_point, AnalysisType::kMax));
    timing_point.set_launch_time(getStartPointLaunchTime(start_point, AnalysisType::kMax));
    timing_point.setLaunchClock(getClockName(start_point));
    seedPathState(start_point, AnalysisType::kMax);
    seedPathState(start_point, AnalysisType::kMin);
  }
  // Keep two complete passes: path seeding may evaluate drivers and populate
  // response caches before physical slew seeding refreshes those same caches.
  seedDataSlewList(startpoints);
}

bool StartpointInitializer::hasIncomingPhysicalSlewArc(std::string& pin_name)
{
  Database& database = _database;
  for (std::size_t arc_idx : database.graph().incomingArcs()[pin_name]) {
    Arc& arc = database.graph().arcs()[arc_idx];
    if (arc.get_type() == ArcType::kNet && !_path_query.isDisableArc(arc)) {
      return true;
    }
  }
  return false;
}

void StartpointInitializer::seedDataSlewList(std::vector<std::string>& startpoints)
{
  for (std::string& start_point : startpoints) {
    seedDataSlew(start_point, AnalysisType::kMax);
    seedDataSlew(start_point, AnalysisType::kMin);
  }
}

void StartpointInitializer::seedDataSlew(std::string& start_point, AnalysisType analysis_type)
{
  seedDataSlew(start_point, analysis_type, TransType::kRise);
  seedDataSlew(start_point, analysis_type, TransType::kFall);
}

void StartpointInitializer::seedDataSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (!allowsTransition(database.network(), database.graph().simValues(), start_point, trans_type, true)) {
    return;
  }
  TimingPoint& timing_point = database.graph().driverPoint(start_point);
  const auto* clock_state = timing_point.findDefaultClockState();
  if (_path_query.isSequentialClockPin(start_point) && hasIncomingPhysicalSlewArc(start_point)
      && (clock_state == nullptr || !clock_state->slew(analysis_type, trans_type).has_value())) {
    // A propagated divider or gated-clock path will provide the physical slew
    // through this net arc. Seeding zero here would win min-slew selection.
    return;
  }
  timing_point.get_data_slew_map()[analysis_type][trans_type] = getStartPointSlew(start_point, analysis_type, trans_type);
}

double StartpointInitializer::getStartPointArrival(std::string& start_point, AnalysisType analysis_type)
{
  return getStartPointArrival(start_point, analysis_type, TransType::kRise);
}

double StartpointInitializer::getStartPointArrival(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (isClockSourceStartPoint(start_point)) {
    return getStartPointClockEdge(start_point, analysis_type, trans_type);
  }
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port()) {
    double arrival = 0.0;
    const auto delays = getInputDelayList(start_point, analysis_type, trans_type);
    bool has_arrival = false;
    for (const auto& resolved_delay : delays) {
      const auto* delay = &resolved_delay;
      const double candidate = getInputDelayArrival(*delay, analysis_type);
      if (!has_arrival || _path_query.isBetterArrival(candidate, arrival, analysis_type)) {
        arrival = candidate;
        has_arrival = true;
      }
    }
    // set_driving_cell contributes its load-dependent delay after the input
    // delay at the port.  calculateDrivingCell returns only this incremental
    // delay (the intrinsic part is removed), so adding it here does not
    // duplicate the physical PAD input arc propagated afterward.
    const std::optional<DelayResult> driving_cell_timing = getDrivingCellTiming(start_point, analysis_type, trans_type);
    return driving_cell_timing ? arrival + driving_cell_timing->get_delay() : arrival;
  }
  if (database.network().instances().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    return launchClockEdge(database, start_point, getClockName(start_point))
           + _path_query.getClockArrival(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
  }
  if (instance.get_is_sequential() && start_point == instance.get_output_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    double clock_slew = _path_query.getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
    auto dc_result = _delay.cellDelay(start_point, instance.get_clock_to_q_arc(), analysis_type, clock_trans_type, trans_type, clock_slew);
    if (dc_result.has_value()) {
      return launchClockEdge(database, start_point, getClockName(start_point))
             + _path_query.getClockArrival(instance.get_clock_pin_name(), analysis_type, clock_trans_type)
             + _path_query.deratedDelay(dc_result->get_delay(), analysis_type, false, trans_type, DerateDelay::kCell, start_point);
    }
  }
  return 0.0;
}

double StartpointInitializer::getStartPointArrival(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                                   std::optional<TransType> source)
{
  Database& database = _database;
  if (isClockSourceStartPoint(start_point)) {
    return getClockEdge(clock_name, trans_type);
  }
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || !database.network().instances().contains(pin.get_instance_name())) {
    return getStartPointArrival(start_point, analysis_type, trans_type);
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_clock_pin_name() && start_point != instance.get_output_pin_name())) {
    return getStartPointArrival(start_point, analysis_type, trans_type);
  }
  const TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
  const double clock_arrival = getClockEdge(clock_name, source.value_or(clock_trans_type))
                               + _path_query.getClockArrival(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type, source);
  if (start_point == instance.get_clock_pin_name())
    return clock_arrival;

  auto dc_result = _delay.cellDelay(start_point, instance.get_clock_to_q_arc(), analysis_type, clock_trans_type, trans_type,
                                    _path_query.getClockSlew(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type, source));
  return dc_result.has_value()
             ? clock_arrival + _path_query.deratedDelay(dc_result->get_delay(), analysis_type, false, trans_type, DerateDelay::kCell, start_point)
             : clock_arrival;
}

std::vector<TimingIoDelay> StartpointInitializer::getInputDelayList(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  const auto constraint = database.sdc().get_port_constraint_map().find(start_point);
  if (constraint == database.sdc().get_port_constraint_map().end()) {
    return {};
  }
  std::vector<const TimingIoDelay*> delays = constraint->second.get_input_delays(analysis_type, trans_type);
  if (delays.empty() && analysis_type == AnalysisType::kMin) {
    delays = constraint->second.get_input_delays(AnalysisType::kMax, trans_type);
  }
  return resolveIoDelays(database, delays);
}

double StartpointInitializer::getClockEdge(std::string_view clock_name, TransType trans_type)
{
  Database& database = _database;
  const auto clock = database.sdc().get_clock_map().find(std::string(clock_name));
  if (clock == database.sdc().get_clock_map().end()) {
    return 0.0;
  }
  return trans_type == TransType::kFall ? clock->second.get_fall_edge() : clock->second.get_rise_edge();
}

double StartpointInitializer::getInputDelayClockTime(const TimingIoDelay& delay, AnalysisType analysis_type, std::optional<TransType> source)
{
  double clock_time = getClockEdge(delay.get_clock_name(), source.value_or(delay.get_clock_trans_type()));
  if (!delay.get_reference_pin().empty()) {
    std::string reference_pin = delay.get_reference_pin();
    const auto values
        = clockPinValues(_database, reference_pin, delay.get_clock_name(), analysis_type, delay.get_clock_trans_type(), AnalysisType::kNone, source);
    clock_time += values.propagated ? _path_query.getClockArrival(reference_pin, delay.get_clock_name(), analysis_type, delay.get_clock_trans_type(), source)
                                    : values.source_latency;
    return clock_time;
  }
  const auto& clocks = _database.sdc().get_clock_map();
  const auto clock = clocks.find(delay.get_clock_name());
  if (clock != clocks.end()) {
    if (!delay.get_source_latency_included()) {
      clock_time += clock->second.get_source_latency(analysis_type, clockVariation(analysis_type), delay.get_clock_trans_type());
    }
    if (!delay.get_network_latency_included() && !clock->second.get_is_propagated()) {
      clock_time += clock->second.get_network_latency(analysis_type, delay.get_clock_trans_type());
    }
  }
  return clock_time;
}

double StartpointInitializer::getInputDelayArrival(const TimingIoDelay& delay, AnalysisType analysis_type, std::optional<TransType> source)
{
  return getInputDelayClockTime(delay, analysis_type, source) + delay.get_delay();
}

bool StartpointInitializer::isClockSourceStartPoint(std::string& start_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  return pin.get_is_port() && getStartPointClock(start_point) != nullptr;
}

TimingClock* StartpointInitializer::getStartPointClock(std::string& start_point)
{
  Database& database = _database;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.sdc().get_clock_map()) {
    for (std::string& clock_source : clock_pair.second.get_source_list()) {
      if (clock_source == start_point) {
        return &clock_pair.second;
      }
    }
  }
  return nullptr;
}

double StartpointInitializer::getStartPointClockEdge(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
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

double StartpointInitializer::getStartPointSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
    if (port_constraint_map.count(start_point) == 0) {
      return 0.0;
    }
    TimingPortConstraint& port_constraint = port_constraint_map[start_point];
    // Explicit port transition waveforms override cell output slew, including
    // default zero on an unspecified edge. The cell still supplies delay.
    if (port_constraint.get_has_input_transition())
      return port_constraint.get_input_transition(analysis_type, trans_type);
    if (port_constraint.get_has_driving_cell(analysis_type, trans_type)) {
      std::optional<DelayResult> driving_cell_timing = getDrivingCellTiming(start_point, analysis_type, trans_type);
      return driving_cell_timing ? driving_cell_timing->get_slew() : 0.0;
    }
    if (port_constraint.get_has_input_transition(analysis_type, trans_type)) {
      return port_constraint.get_input_transition(analysis_type, trans_type);
    }
    return 0.0;
  }
  if (database.network().instances().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    return _path_query.getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
  }
  if (instance.get_is_sequential() && start_point == instance.get_output_pin_name()) {
    TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
    double clock_slew = _path_query.getClockSlew(instance.get_clock_pin_name(), analysis_type, clock_trans_type);
    auto dc_result = _delay.cellDelay(start_point, instance.get_clock_to_q_arc(), analysis_type, clock_trans_type, trans_type, clock_slew);
    if (dc_result.has_value()) {
      return dc_result->get_slew();
    }
  }
  return 0.0;
}

double StartpointInitializer::getStartPointSlew(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                                std::optional<TransType> source)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || !database.network().instances().contains(pin.get_instance_name())) {
    return getStartPointSlew(start_point, analysis_type, trans_type);
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_clock_pin_name() && start_point != instance.get_output_pin_name())) {
    return getStartPointSlew(start_point, analysis_type, trans_type);
  }
  const TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
  const double clock_slew = _path_query.getClockSlew(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type, source);
  if (start_point == instance.get_clock_pin_name())
    return clock_slew;

  auto dc_result = _delay.cellDelay(start_point, instance.get_clock_to_q_arc(), analysis_type, clock_trans_type, trans_type, clock_slew);
  return dc_result.has_value() ? dc_result->get_slew() : clock_slew;
}

std::optional<DelayResult> StartpointInitializer::getDrivingCellTiming(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
  if (!port_constraint_map.contains(start_point)) {
    return std::nullopt;
  }
  TimingDrivingCell* driving_cell = port_constraint_map.at(start_point).get_driving_cell(analysis_type, trans_type);
  if (driving_cell == nullptr) {
    return std::nullopt;
  }
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (!timing_cell_map.contains(driving_cell->get_cell_name())) {
    return std::nullopt;
  }
  LibertyCell& timing_cell = timing_cell_map.at(driving_cell->get_cell_name());
  if (timing_cell.get_library_name() != driving_cell->get_library_name()) {
    return std::nullopt;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() != driving_cell->get_from_pin() || timing_cell_arc.get_sink_port() != driving_cell->get_to_pin()) {
      continue;
    }
    DelayResult timing_result;
    if (_delay.calculateDrivingCell(start_point, timing_cell_arc, analysis_type, trans_type, driving_cell->get_input_transition_rise(),
                                    driving_cell->get_input_transition_fall(), timing_result)) {
      return timing_result;
    }
  }
  return std::nullopt;
}

double StartpointInitializer::getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type)
{
  return getStartPointLaunchTime(start_point, analysis_type, TransType::kRise);
}

double StartpointInitializer::getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (isClockSourceStartPoint(start_point)) {
    return getStartPointClockEdge(start_point, analysis_type, trans_type);
  }
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return launchClockEdge(database, start_point, getClockName(start_point));
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return 0.0;
  }
  return launchClockEdge(database, start_point, getClockName(start_point))
         + _path_query.getClockArrival(instance.get_clock_pin_name(), analysis_type, getClockTransType(instance.get_clock_to_q_arc()));
}

double StartpointInitializer::getStartPointLaunchTime(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                                      std::optional<TransType> source)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || !database.network().instances().contains(pin.get_instance_name())) {
    return getClockEdge(clock_name, trans_type);
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name()))
    return 0.0;
  const TransType clock_trans_type = getClockTransType(instance.get_clock_to_q_arc());
  return getClockEdge(clock_name, source.value_or(clock_trans_type))
         + _path_query.getClockArrival(instance.get_clock_pin_name(), clock_name, analysis_type, clock_trans_type, source);
}

std::string StartpointInitializer::getStartPointCrprClockPin(std::string& start_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return "";
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return "";
  }
  std::string clock_pin_name = instance.get_clock_pin_name();
  return clock_pin_name;
}

TransType StartpointInitializer::getStartPointCrprClockTransType(std::string& start_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return TransType::kNone;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (!instance.get_is_sequential() || (start_point != instance.get_output_pin_name() && start_point != instance.get_clock_pin_name())) {
    return TransType::kNone;
  }
  return getClockTransType(instance.get_clock_to_q_arc());
}

TransType StartpointInitializer::getClockTransType(TimingCellArc& timing_cell_arc)
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

std::string_view StartpointInitializer::getClockName(std::string& pin_name)
{
  Database& database = _database;
  TimingClock* timing_clock = getStartPointClock(pin_name);
  if (timing_clock != nullptr) {
    return timing_clock->get_clock_name();
  }
  Pin& pin = database.network().pins()[pin_name];
  if (!pin.get_is_port() && database.network().instances().count(pin.get_instance_name()) > 0) {
    Instance& instance = database.network().instances()[pin.get_instance_name()];
    if (instance.get_is_sequential() && database.graph().points().count(instance.get_clock_pin_name()) > 0) {
      const auto& propagated_clock_name = database.graph().points()[instance.get_clock_pin_name()].launchClock();
      if (!propagated_clock_name.empty()) {
        return propagated_clock_name;
      }
    }
  }
  if (database.graph().points().count(pin_name) > 0) {
    TimingPoint& timing_point = database.graph().points()[pin_name];
    if (timing_point.get_is_clock_point() && !timing_point.launchClock().empty()) {
      return timing_point.launchClock();
    }
  }
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
    if (port_constraint_map.count(pin_name) > 0 && !port_constraint_map[pin_name].inputClock().empty()) {
      return port_constraint_map[pin_name].inputClock();
    }
  }
  return {};
}

std::vector<std::string> StartpointInitializer::getStartPointClockNames(std::string& start_point)
{
  Database& database = _database;
  std::set<std::string> names;
  for (const auto& [clock_name, clock] : database.sdc().get_clock_map()) {
    if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), start_point) != clock.get_source_list().end())
      names.insert(clock_name);
  }
  Pin& pin = database.network().pins()[start_point];
  if (!pin.get_is_port() && database.network().instances().contains(pin.get_instance_name())) {
    Instance& instance = database.network().instances()[pin.get_instance_name()];
    if (instance.get_is_sequential() && database.graph().points().contains(instance.get_clock_pin_name())) {
      const auto& states = database.graph().points().at(instance.get_clock_pin_name()).get_clock_state_map();
      for (const auto& [clock_name, state] : states)
        if (!state.is_source_path_only)
          names.insert(clock_name);
    }
  }
  if (names.empty())
    names.insert(std::string(getClockName(start_point)));
  return {names.begin(), names.end()};
}

std::string StartpointInitializer::getPathStateStartPoint(std::string& start_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return start_point;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  if (instance.get_is_sequential() && start_point == instance.get_clock_pin_name()) {
    return instance.get_output_pin_name();
  }
  return start_point;
}

void StartpointInitializer::seedPathState(std::string& start_point, AnalysisType analysis_type)
{
  PathSourceType source_type = getStartPointSourceType(start_point, analysis_type);
  if (source_type == PathSourceType::kNone) {
    return;
  }
  Database& database = _database;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    if (!allowsTransition(database.network(), database.graph().simValues(), start_point, trans_type, true)) {
      continue;
    }
    const auto input_delays = getInputDelayList(start_point, analysis_type, trans_type);
    if (isInputStartPoint(start_point) && (!input_delays.empty() || !isClockSourceStartPoint(start_point))) {
      for (const auto& resolved_delay : input_delays) {
        const auto* delay = &resolved_delay;
        seedInputPathState(start_point, analysis_type, trans_type, *delay);
      }
      continue;
    }
    for (const std::string& clock_name : getStartPointClockNames(start_point)) {
      const bool clock_source = isClockSourceStartPoint(start_point);
      const auto local = getStartPointCrprClockTransType(start_point);
      const auto pin = getStartPointCrprClockPin(start_point);
      const auto origins = clock_source ? std::vector<TransType>{trans_type} : clockSourceTransitions(database, pin, clock_name, analysis_type, local);
      const auto definition = database.sdc().get_clock_map().find(clock_name);
      for (auto origin : origins) {
        const auto first = origin == TransType::kFall ? 1u : 0u;
        const auto count = definition == database.sdc().get_clock_map().end() ? 2 : definition->second.edgeCount();
        const double arrival = getStartPointArrival(start_point, clock_name, analysis_type, trans_type, origin);
        const double slew = getStartPointSlew(start_point, clock_name, analysis_type, trans_type, origin);
        const double launch = getStartPointLaunchTime(start_point, clock_name, analysis_type, trans_type, origin);
        for (size_t index = first; index < count; index += 2) {
          const double offset
              = definition == database.sdc().get_clock_map().end() ? 0. : definition->second.edge(index).time - definition->second.edge(first).time;
          seedPathState(start_point, analysis_type, trans_type, clock_name, arrival + offset, slew, launch + offset, local, clock_source, std::nullopt, 0.,
                        index);
        }
      }
    }
  }
}

void StartpointInitializer::seedInputPathState(std::string& start_point, AnalysisType analysis_type, TransType trans_type, const TimingIoDelay& delay)
{
  const auto& clock_name = delay.get_clock_name();
  const auto local = delay.get_clock_trans_type();
  const auto origins = delay.get_reference_pin().empty() ? std::vector<TransType>{local}
                                                         : clockSourceTransitions(_database, delay.get_reference_pin(), clock_name, analysis_type, local);
  const auto driver = getDrivingCellTiming(start_point, analysis_type, trans_type);
  const auto clock = _database.sdc().get_clock_map().find(clock_name);
  for (auto source : origins) {
    const double launch = getInputDelayClockTime(delay, analysis_type, source);
    double source_latency = 0.;
    if (!delay.get_source_latency_included() && clock != _database.sdc().get_clock_map().end())
      source_latency = clockPinValues(_database, delay.get_reference_pin(), clock_name, analysis_type, local, AnalysisType::kNone, source).source_latency;
    const size_t first = source == TransType::kFall ? 1 : 0;
    const size_t count = clock == _database.sdc().get_clock_map().end() ? 2 : clock->second.edgeCount();
    for (size_t index = first; index < count; index += 2) {
      const double offset = clock == _database.sdc().get_clock_map().end() ? 0. : clock->second.edge(index).time - clock->second.edge(first).time;
      seedPathState(start_point, analysis_type, trans_type, clock_name, launch + offset + delay.get_delay() + (driver ? driver->get_delay() : 0.0),
                    getStartPointSlew(start_point, analysis_type, trans_type), launch + offset, local, false, delay.get_delay(), source_latency, index,
                    delay.get_reference_pin());
    }
  }
}

void StartpointInitializer::seedPathState(std::string& start_point, AnalysisType analysis_type, TransType trans_type, std::string_view clock_name,
                                          double arrival, double slew, double launch_time, TransType clock_trans_type, bool clock_source_path,
                                          std::optional<double> input_delay, double input_source_latency, std::optional<std::size_t> edge_index,
                                          std::string_view reference_pin)
{
  if (!edge_index && !clock_name.empty()) {
    const auto transition = clock_source_path ? trans_type : clock_trans_type;
    const auto clock = _database.sdc().get_clock_map().find(std::string(clock_name));
    const size_t first = transition == TransType::kFall ? 1 : 0;
    const size_t count = clock == _database.sdc().get_clock_map().end() ? 2 : clock->second.edgeCount();
    for (size_t index = first; index < count; index += 2) {
      const double offset = clock == _database.sdc().get_clock_map().end() ? 0 : clock->second.edge(index).time - clock->second.edge(first).time;
      seedPathState(start_point, analysis_type, trans_type, clock_name, arrival + offset, slew, launch_time + offset, clock_trans_type, clock_source_path,
                    input_delay, input_source_latency, index, reference_pin);
    }
    return;
  }
  Database& database = _database;
  PathSourceType source_type = getStartPointSourceType(start_point, analysis_type);
  if (source_type == PathSourceType::kNone) {
    return;
  }
  std::string path_state_start_point = getPathStateStartPoint(start_point);
  TimingPoint& timing_point = database.graph().driverPoint(start_point);
  const auto launch_transition = edge_index ? (*edge_index % 2 ? TransType::kFall : TransType::kRise) : (clock_source_path ? trans_type : clock_trans_type);
  const std::vector<int32_t> initial_exception_state_list
      = ExceptionMatcher(database.sdc(), database.network()).initState(path_state_start_point, clock_name, trans_type, analysis_type, launch_transition);
  const std::vector<int32_t> exception_state_list
      = ExceptionMatcher(database.sdc(), database.network()).advanceState(_through_index, initial_exception_state_list, start_point, trans_type);
  const std::string crpr_pin = reference_pin.empty() ? getStartPointCrprClockPin(start_point) : std::string(reference_pin);
  const bool preserve_crpr = _path_query.clockPathHasVariation(crpr_pin, clock_name, clock_trans_type, launch_transition);
  const std::string path_state_tag
      = ExceptionMatcher(database.sdc(), database.network())
            .makePathStateTag(clock_name, launch_transition, exception_state_list, clock_source_path, preserve_crpr ? crpr_pin : "", edge_index);
  std::map<std::string, TimingPathState>& path_state_map = _path_query.getPathStateMap(timing_point, analysis_type, source_type, trans_type);
  if (path_state_map.count(path_state_tag) > 0 && !_path_query.isBetterArrival(arrival, path_state_map[path_state_tag].get_arrival(), analysis_type)) {
    return;
  }

  double source_latency = input_source_latency;
  if (source_type == PathSourceType::kRegister) {
    const auto clock = database.sdc().get_clock_map().find(std::string(clock_name));
    if (clock != database.sdc().get_clock_map().end())
      source_latency = clockPinValues(_database, crpr_pin, clock_name, analysis_type, clock_trans_type, AnalysisType::kNone, launch_transition).source_latency;
  }
  TimingPathState& path_state = path_state_map[path_state_tag];
  path_state.set_arrival(arrival);
  path_state.set_slew(slew);
  path_state.set_launch_time(launch_time);
  path_state.set_input_delay(input_delay);
  path_state.set_launch_source_latency(source_latency);
  path_state.set_launch_clock_trans_type(launch_transition);
  if (edge_index)
    path_state.set_launch_clock_edge_index(*edge_index);
  path_state.set_start_point(path_state_start_point);
  path_state.set_clock_name(clock_name);
  path_state.set_is_clock_source_path(clock_source_path);
  path_state.set_crpr_clock_pin(crpr_pin);
  path_state.set_preserve_crpr_path(preserve_crpr);
  path_state.set_path_state_tag(path_state_tag);
  path_state.set_predecessor_path_state_tag("");
  path_state.set_exception_state_list(exception_state_list);
  path_state.set_predecessor("");
  path_state.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
  path_state.set_predecessor_arc_delay(0.0);
  path_state.set_trans_type(trans_type);
  path_state.set_predecessor_trans_type(TransType::kNone);
  path_state.set_crpr_clock_trans_type(clock_trans_type);
}

PathSourceType StartpointInitializer::getStartPointSourceType(std::string& start_point, AnalysisType analysis_type)
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

bool StartpointInitializer::hasInputDelay(std::string& start_point, AnalysisType analysis_type)
{
  Database& database = _database;
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
  if (port_constraint_map.count(start_point) == 0) {
    return false;
  }
  TimingPortConstraint& port_constraint = port_constraint_map[start_point];
  if (analysis_type == AnalysisType::kMin) {
    return port_constraint.get_has_input_delay_min() || port_constraint.get_has_input_delay_max();
  }
  return port_constraint.get_has_input_delay_max() || port_constraint.get_has_input_delay_min();
}

bool StartpointInitializer::isInputStartPoint(std::string& start_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

bool StartpointInitializer::isRegisterStartPoint(std::string& start_point)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[start_point];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && (start_point == instance.get_output_pin_name() || start_point == instance.get_clock_pin_name())
         && hasClockPoint(instance.get_clock_pin_name());
}

bool StartpointInitializer::hasClockPoint(std::string& pin_name)
{
  Database& database = _database;
  return database.graph().points().count(pin_name) > 0 && database.graph().points()[pin_name].get_is_clock_point();
}
}  // namespace ista
