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
#include "PathChecker.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ClockReconvergenceCalculator.hpp"
#include "DelayCalculator.hpp"
#include "TimingTolerance.hpp"
#include "sdc/ClockEdges.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {

std::string_view PathChecker::getClockName(const std::string& pin_name)
{
  if (auto* clock = _query.getStartPointClock(pin_name))
    return clock->get_clock_name();
  const auto& pins = _database.network().pins();
  const auto pin = pins.find(pin_name);
  const auto& points = _database.graph().points();
  const auto point = points.find(pin_name);
  if (point != points.end() && point->second.get_is_clock_point() && !point->second.launchClock().empty())
    return point->second.launchClock();
  if (pin != pins.end() && !pin->second.get_is_port()) {
    const auto instance = _database.network().instances().find(pin->second.get_instance_name());
    if (instance != _database.network().instances().end() && instance->second.get_is_sequential()) {
      const auto clock_point = points.find(instance->second.get_clock_pin_name());
      if (clock_point != points.end() && !clock_point->second.launchClock().empty())
        return clock_point->second.launchClock();
    }
  }
  if (pin != pins.end() && pin->second.get_is_port()) {
    const auto& ports = _database.sdc().get_port_constraint_map();
    const auto port = ports.find(pin_name);
    if (port != ports.end()) {
      const auto& clock = port->second.inputClock().empty() ? port->second.outputClock() : port->second.inputClock();
      if (!clock.empty())
        return clock;
    }
  }
  return {};
}

std::vector<std::string> PathChecker::getCaptureClockNames(const std::string& clock_pin)
{
  std::vector<std::string> names;
  const auto point = _database.graph().points().find(clock_pin);
  if (point != _database.graph().points().end()) {
    for (const auto& [clock_name, state] : point->second.get_clock_state_map())
      if (!state.is_source_path_only)
        names.push_back(clock_name);
  }
  if (names.empty())
    names.emplace_back(getClockName(clock_pin));
  return names;
}

double PathChecker::getClockUncertainty(std::string_view launch_clock, TransType launch_trans_type, std::string_view capture_clock,
                                        TransType capture_trans_type, AnalysisType analysis_type, const std::string& capture_pin,
                                        std::optional<TransType> local)
{
  Database& database = _database;
  Sdc& constraint = database.sdc();
  const std::optional<double> pair_uncertainty
      = constraint.resolve_clock_uncertainty_value(launch_clock, launch_trans_type, capture_clock, capture_trans_type, analysis_type);
  if (pair_uncertainty.has_value()) {
    return *pair_uncertainty;
  }
  return clockPinValues(_database, capture_pin, capture_clock, getCaptureAnalysisType(analysis_type), local.value_or(capture_trans_type), analysis_type,
                        capture_trans_type)
      .uncertainty;
}

std::vector<PathEnd> PathChecker::checkPathCandidates(const TimingPathState& path, const std::string& end_point, AnalysisType analysis,
                                                      bool include_reconvergence)
{
  std::vector<PathCheck> checks;
  const bool is_output = _database.network().pins().at(end_point).get_is_port();
  if (is_output) {
    checks = evaluateOutput(path, end_point, analysis, path.get_trans_type(), include_reconvergence);
  } else {
    for (auto* arc : getCheckArcs(end_point, analysis)) {
      if (!isMatchCheckTransType(*arc, path.get_trans_type()))
        continue;
      for (const auto& clock : getCaptureClockNames(arc->get_clock_port())) {
        const auto definition = _database.sdc().get_clock_map().find(clock);
        const size_t count = definition == _database.sdc().get_clock_map().end() ? 2 : definition->second.edgeCount();
        for (auto source : clockSourceTransitions(_database, arc->get_clock_port(), clock, getCaptureAnalysisType(analysis), arc->get_clock_trans_type()))
          for (size_t index = source == TransType::kFall ? 1 : 0; index < count; index += 2)
            checks.push_back(evaluateRegister(path, *arc, analysis, clock, index, include_reconvergence));
      }
    }
  }
  std::vector<PathEnd> candidates;
  const ExceptionMatcher matcher(_database.sdc(), _database.network());
  if (checks.empty()) {
    // A path-delay exception can constrain an otherwise unconstrained endpoint.
    // Missing I/O edges must not acquire a delay from another edge's summary.
    PathCheck unconstrained;
    // An explicit path delay supplies its own time anchor. No periodic
    // relationship or endpoint margin exists for this fallback.

    unconstrained.capture_clock = is_output ? getOutputClockName(end_point) : getClockName(end_point);
    if (matcher
            .resolve(path.get_start_point(), path.get_clock_name(), end_point, unconstrained.capture_clock, analysis, path.get_trans_type(),
                     unconstrained.capture_transition, path.get_exception_state_list())
            .path_delay
        != nullptr)
      checks.push_back(std::move(unconstrained));
  }
  for (auto& check : checks) {
    const auto& clocks = _database.sdc().get_clock_map();
    const bool clock_relation = clocks.contains(path.get_clock_name()) && clocks.contains(check.capture_clock);
    if (!clock_relation
        && matcher.resolve(path.get_start_point(), path.get_clock_name(), end_point, check.capture_clock, analysis, path.get_trans_type(),
                           check.capture_transition, path.get_exception_state_list())
                   .path_delay
               == nullptr)
      continue;
    if (matcher.isFalsePath(path.get_start_point(), path.get_clock_name(), end_point, check.capture_clock, analysis, path.get_trans_type(),
                            check.capture_transition, path.get_exception_state_list()))
      continue;
    const double required = calcPathRequiredTime(end_point, path, analysis, check);
    candidates.emplace_back(path, std::move(check), required, calcPathSlack(path, required, analysis));
  }
  return candidates;
}

std::optional<PathEnd> PathChecker::checkPath(const TimingPathState& path, const std::string& end_point, AnalysisType analysis,
                                               bool include_reconvergence)
{
  auto candidates = checkPathCandidates(path, end_point, analysis, include_reconvergence);
  if (candidates.empty())
    return std::nullopt;

  const bool is_output = _database.network().pins().at(end_point).get_is_port();
  std::optional<PathEnd> worst;
  for (const auto& candidate : candidates) {
    // Preserve the previous inner capture reduction independently of path
    // ordering. Candidate enumeration is now shared with min/hold export.
    if (!worst || candidate.slack() < worst->slack() - (is_output ? 0.0 : kTimingTolerance))
      worst = candidate;
  }
  return worst;
}

PathCheck PathChecker::evaluateRegister(const TimingPathState& path, TimingCheckArc& arc, AnalysisType analysis_type, std::string_view capture_clock,
                                        std::size_t capture_index, bool include_reconvergence)
{
  const TransType launch_transition = path.get_launch_clock_trans_type();
  PathCheck result;
  result.capture_clock = capture_clock;
  result.type = arc.get_check_type();
  result.capture_pin_transition = arc.get_clock_trans_type();
  result.capture_transition = capture_index % 2 ? TransType::kFall : TransType::kRise;
  result.capture_edge_index = capture_index;
  result.capture_pin = arc.get_clock_port();
  const auto capture_mode = getCaptureAnalysisType(analysis_type);
  const auto check = checkTime(path, arc, analysis_type, capture_clock, result.capture_transition);
  result.check_time = check.value;
  result.selected_check_arc_idx = check.selected_arc_idx;
  if (include_reconvergence)
    result.reconvergence = getClockReconvergencePessimism(path, result, analysis_type);
  if (analysis_type == AnalysisType::kMin)
    result.reconvergence = -result.reconvergence;
  result.uncertainty = getClockUncertainty(path.get_clock_name(), launch_transition, capture_clock, result.capture_transition, analysis_type,
                                           result.capture_pin, result.capture_pin_transition);
  const double arrival = _query.getClockArrival(result.capture_pin, capture_clock, capture_mode, result.capture_pin_transition, result.capture_transition);
  const auto clock = _database.sdc().get_clock_map().find(std::string(capture_clock));
  if (clock != _database.sdc().get_clock_map().end())
    result.source_latency = _clock_pin_values.values(result.capture_pin, capture_clock, capture_mode, result.capture_pin_transition, AnalysisType::kNone,
                                                     result.capture_transition)
                                .source_latency;
  result.network_delay = arrival - result.source_latency;
  result.capture_time = getEndPointCaptureTime(path, capture_clock, result.capture_transition, analysis_type, capture_index) + arrival + result.reconvergence;
  const double signed_check = analysis_type == AnalysisType::kMin ? result.check_time : -result.check_time;
  result.required_before_round = result.capture_time + signed_check + (analysis_type == AnalysisType::kMin ? result.uncertainty : -result.uncertainty);
  result.required = _query.roundTime(result.required_before_round);
  return result;
}

PathChecker::CheckTimeResult PathChecker::checkTime(const TimingPathState& path, TimingCheckArc& arc, AnalysisType analysis, std::string_view clock,
                                                    TransType source)
{
  // Graph-based checks use the extrema of the physical waveform range.
  // A smaller slew may produce a larger check in a non-monotonic Liberty
  // table; the arrival-winning path's slew alone is insufficient.
  std::vector<double> data_slews{path.get_slew()};
  std::vector<double> clock_slews{_query.getClockSlew(arc.get_clock_port(), clock, getCaptureAnalysisType(analysis), arc.get_clock_trans_type(), source)};
  const auto data_point = _database.graph().points().find(arc.get_data_port());
  const auto clock_point = _database.graph().points().find(arc.get_clock_port());
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    if (data_point != _database.graph().points().end()) {
      const auto slews = data_point->second.get_data_slew_map().find(mode);
      if (slews != data_point->second.get_data_slew_map().end() && slews->second.contains(path.get_trans_type()))
        data_slews.push_back(slews->second.at(path.get_trans_type()));
    }
    if (clock_point != _database.graph().points().end())
      if (const auto* state = clockWaveform(_database, arc.get_clock_port(), clock, source))
        if (auto slew = state->slew(mode, arc.get_clock_trans_type()))
          clock_slews.push_back(*slew);
  }
  auto deduplicate = [](auto& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
  };
  deduplicate(data_slews);
  deduplicate(clock_slews);
  std::optional<CheckTimeResult> result;
  for (double data_slew : data_slews)
    for (double clock_slew : clock_slews) {
      // Keep the same candidate reduction as CheckDelayCalculator, but also
      // retain the Liberty variant that produced the reduced value.  The
      // outer reduction over data/clock slew extrema is intentionally still
      // max-of-check, matching the existing PathChecker semantics for both
      // setup/recovery and hold/removal.
      std::optional<CheckTimeResult> candidate_result;
      for (TimingArc& timing_arc : arc.get_timing_arc_list()) {
        const auto table = timing_arc.get_check_table_map().find(path.get_trans_type());
        if (table == timing_arc.get_check_table_map().end())
          continue;
        const double nominal
            = table->second.findValue(clock_slew * timing_arc.get_time_unit_scale(), data_slew * timing_arc.get_time_unit_scale())
              / timing_arc.get_time_unit_scale();
        if (!candidate_result
            || (analysis == AnalysisType::kMin ? nominal < candidate_result->value : nominal > candidate_result->value)) {
          candidate_result = CheckTimeResult{nominal, timing_arc.get_arc_idx()};
        }
      }

      const double nominal = candidate_result ? candidate_result->value : arc.get_check_time();
      const double check = _query.deratedDelay(nominal, analysis, false, path.get_trans_type(), DerateDelay::kCheck, arc.get_data_port());
      if (!result || check > result->value)
        result = CheckTimeResult{check, candidate_result ? candidate_result->selected_arc_idx : -1};
    }
  return result.value_or(CheckTimeResult{_query.deratedDelay(arc.get_check_time(), analysis, false, path.get_trans_type(), DerateDelay::kCheck, arc.get_data_port()), -1});
}

bool PathChecker::isMatchCheckTransType(TimingCheckArc& timing_check_arc, TransType data_trans_type)
{
  if (timing_check_arc.get_timing_arc_list().empty()) {
    return true;
  }
  for (TimingArc& timing_arc : timing_check_arc.get_timing_arc_list()) {
    if (timing_arc.get_check_table_map().count(data_trans_type) > 0) {
      return true;
    }
  }
  return false;
}

AnalysisType PathChecker::getCaptureAnalysisType(AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMax) {
    return AnalysisType::kMin;
  }
  if (analysis_type == AnalysisType::kMin) {
    return AnalysisType::kMax;
  }
  return analysis_type;
}

double PathChecker::getEndPointCaptureTime(const TimingPathState& path, std::string_view capture_clock, TransType capture_transition,
                                           AnalysisType analysis_type, std::size_t capture_index, bool level_sensitive)
{
  const auto& clocks = _database.sdc().get_clock_map();
  const auto launch = clocks.find(path.get_clock_name());
  const auto capture = clocks.find(std::string(capture_clock));
  if (launch == clocks.end() || capture == clocks.end())
    return 0.0;
  const double launch_edge
      = launch->second.edge(path.get_launch_clock_edge_index().value_or(path.get_launch_clock_trans_type() == TransType::kFall ? 1 : 0)).time;
  const double capture_edge = capture->second.edge(capture_index).time;
  return launch_edge
         + clockEdgeSeparation(launch->second.get_period(), capture->second.get_period(), launch_edge, capture_edge, analysis_type, level_sensitive);
}

double PathChecker::getClockEdge(std::string_view clock_name, TransType trans_type)
{
  Database& database = _database;
  const auto clock = database.sdc().get_clock_map().find(std::string(clock_name));
  if (clock == database.sdc().get_clock_map().end()) {
    return 0.0;
  }
  return trans_type == TransType::kFall ? clock->second.get_fall_edge() : clock->second.get_rise_edge();
}

std::vector<TimingIoDelay> PathChecker::getOutputDelayList(const std::string& end_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  const auto constraint = database.sdc().get_port_constraint_map().find(end_point);
  if (constraint == database.sdc().get_port_constraint_map().end()) {
    return {};
  }
  return resolveIoDelays(_database, constraint->second.get_output_delays(analysis_type, trans_type));
}

std::string_view PathChecker::getOutputClockName(const std::string& end_point)
{
  const auto& ports = _database.sdc().get_port_constraint_map();
  const auto port = ports.find(end_point);
  if (port != ports.end() && !port->second.outputClock().empty())
    return port->second.outputClock();
  return getClockName(end_point);
}

std::vector<PathCheck> PathChecker::evaluateOutput(const TimingPathState& path, const std::string& end_point, AnalysisType analysis_type,
                                                   TransType data_trans_type, bool include_reconvergence)
{
  auto& database = _database;
  const auto delays = getOutputDelayList(end_point, analysis_type, data_trans_type);
  std::vector<PathCheck> candidates;
  for (const auto& resolved_delay : delays) {
    const auto* delay = &resolved_delay;
    auto local = delay->get_clock_trans_type();
    // The reference pin defines the local edge, including an external latch's opening edge.
    if (delay->get_level_sensitive() && analysis_type == AnalysisType::kMin)
      local = local == TransType::kRise ? TransType::kFall : TransType::kRise;
    const auto origins = delay->get_reference_pin().empty() ? std::vector<TransType>{local}
                                                            : clockSourceTransitions(database, delay->get_reference_pin(), delay->get_clock_name(),
                                                                                     getCaptureAnalysisType(analysis_type), local);
    for (auto source : origins) {
      PathCheck candidate;
      candidate.capture_clock = delay->get_clock_name();
      candidate.capture_transition = source;
      candidate.capture_pin_transition = local;
      candidate.output_delay = delay->get_delay();
      candidate.level_sensitive = delay->get_level_sensitive();
      candidate.uncertainty = getClockUncertainty(path.get_clock_name(), path.get_launch_clock_trans_type(), candidate.capture_clock,
                                                  candidate.capture_transition, analysis_type, delay->get_reference_pin(), local);
      if (!delay->get_reference_pin().empty()) {
        candidate.capture_pin = delay->get_reference_pin();
        const auto mode = getCaptureAnalysisType(analysis_type);
        const auto values = _clock_pin_values.values(candidate.capture_pin, candidate.capture_clock, mode, local, AnalysisType::kNone, source);
        candidate.source_latency = values.source_latency;
        candidate.network_delay
            = values.propagated ? _query.getClockArrival(candidate.capture_pin, candidate.capture_clock, mode, local, source) - values.source_latency : 0.;
        if (include_reconvergence)
          candidate.reconvergence = getClockReconvergencePessimism(path, candidate, analysis_type);
        if (analysis_type == AnalysisType::kMin)
          candidate.reconvergence = -candidate.reconvergence;
      } else {
        const auto clock = database.sdc().get_clock_map().find(candidate.capture_clock);
        if (clock != database.sdc().get_clock_map().end()) {
          const auto mode = getCaptureAnalysisType(analysis_type);
          if (!delay->get_source_latency_included())
            candidate.source_latency = clock->second.get_source_latency(mode, clockVariation(mode), candidate.capture_transition);
          if (!delay->get_network_latency_included() && !clock->second.get_is_propagated())
            candidate.network_delay = clock->second.get_network_latency(mode, candidate.capture_transition);
        }
      }
      const auto clock = database.sdc().get_clock_map().find(candidate.capture_clock);
      const size_t count = clock == database.sdc().get_clock_map().end() ? 2 : clock->second.edgeCount();
      for (size_t index = candidate.capture_transition == TransType::kFall ? 1 : 0; index < count; index += 2) {
        auto indexed = candidate;
        indexed.capture_edge_index = index;
        indexed.capture_time
            = getEndPointCaptureTime(path, indexed.capture_clock, indexed.capture_transition, analysis_type, index, delay->get_level_sensitive());
        // Keep the original source-then-network accumulation order.
        indexed.capture_time += indexed.source_latency;
        indexed.capture_time += indexed.network_delay;
        indexed.capture_time += indexed.reconvergence;
        indexed.required_before_round
            = indexed.capture_time - *indexed.output_delay + (analysis_type == AnalysisType::kMin ? indexed.uncertainty : -indexed.uncertainty);
        indexed.required = _query.roundTime(indexed.required_before_round);
        candidates.push_back(std::move(indexed));
      }
    }
  }
  return candidates;
}

double PathChecker::getClockReconvergencePessimism(const TimingPathState& path, PathCheck& check, AnalysisType analysis)
{
  auto launch_pin = path.get_crpr_clock_pin();
  auto launch_transition = path.get_crpr_clock_trans_type();
  if (launch_pin.empty()) {
    const auto pin = _database.network().pins().find(path.get_start_point());
    if (pin == _database.network().pins().end() || pin->second.get_is_port())
      return 0.0;
    const auto instance = _database.network().instances().find(pin->second.get_instance_name());
    if (instance == _database.network().instances().end() || !instance->second.get_is_sequential())
      return 0.0;
    launch_pin = instance->second.get_clock_pin_name();
    launch_transition = path.get_launch_clock_trans_type();
  }
  if (!_database.network().pins().contains(launch_pin))
    return 0.0;
  if (launch_transition == TransType::kNone)
    launch_transition = TransType::kRise;
  return _reconvergence.correction(launch_pin, launch_transition, path.get_clock_name(), check.capture_pin,
                                   check.capture_pin_transition, check.capture_clock, analysis, check.common_pin,
                                   path.get_launch_clock_trans_type(), check.capture_transition);
}

std::vector<TimingCheckArc*> PathChecker::getCheckArcs(const std::string& end_point, AnalysisType analysis)
{
  std::vector<TimingCheckArc*> arcs;
  const auto& pin = _database.network().pins().at(end_point);
  const auto instance = _database.network().instances().find(pin.get_instance_name());
  if (pin.get_is_port() || instance == _database.network().instances().end())
    return arcs;
  for (auto& arc : instance->second.get_check_arc_list())
    if (arc.get_data_port() == end_point && isMatchCheckType(arc, analysis))
      arcs.push_back(&arc);
  return arcs;
}

bool PathChecker::isMatchCheckType(TimingCheckArc& timing_check_arc, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return timing_check_arc.get_check_type() == TimingCheckType::kHold || timing_check_arc.get_check_type() == TimingCheckType::kRemoval;
  }
  return timing_check_arc.get_check_type() == TimingCheckType::kSetup || timing_check_arc.get_check_type() == TimingCheckType::kRecovery;
}

double PathChecker::calcPathRequiredTime(const std::string& end_point, const TimingPathState& end_path_state, AnalysisType analysis_type,
                                         PathCheck& check)
{
  const ResolvedTimingExceptions exceptions
      = ExceptionMatcher(_database.sdc(), _database.network())
            .resolve(end_path_state.get_start_point(), end_path_state.get_clock_name(), end_point, check.capture_clock, analysis_type,
                     end_path_state.get_trans_type(), check.capture_transition, end_path_state.get_exception_state_list());
  if (exceptions.path_delay != nullptr) {
    return calcPathDelayRequiredTime(end_point, end_path_state, analysis_type, *exceptions.path_delay, check);
  }
  if (exceptions.setup_multicycle != nullptr || exceptions.hold_multicycle != nullptr) {
    check.has_required_adjustment = true;
    check.required_adjustment = multicycleAdjustment(end_path_state, analysis_type, exceptions, check);
    return _query.roundTime(check.required + check.required_adjustment);
  }
  return check.required;
}

double PathChecker::calcPathDelayRequiredTime(const std::string& end_point, const TimingPathState& end_path_state, AnalysisType analysis_type,
                                              const TimingException& exception, const PathCheck& check)
{
  Database& database = _database;
  const TransType launch_trans_type = end_path_state.get_launch_clock_trans_type();
  const auto& capture_clock = check.capture_clock;
  const auto capture_transition = check.capture_transition;
  const double capture_arrival
      = _database.network().pins().at(end_point).get_is_port() || check.capture_pin.empty()
            ? 0.0
            : _query.getClockArrival(check.capture_pin, capture_clock, getCaptureAnalysisType(analysis_type), check.capture_pin_transition, capture_transition);
  const double normal_capture_time
      = getEndPointCaptureTime(end_path_state, capture_clock, capture_transition, analysis_type,
                               check.capture_edge_index.value_or(capture_transition == TransType::kFall ? 1 : 0), check.level_sensitive)
        + capture_arrival;
  double endpoint_margin = check.required - normal_capture_time;
  if (database.network().pins().at(end_point).get_is_port() && !check.output_delay)
    endpoint_margin = 0.0;

  double anchor = end_path_state.get_launch_time();
  if (exception.get_ignore_clock_latency()) {
    // A clock-independent path budget excludes capture clock latency and
    // uncertainty, while retaining the endpoint check or output-delay margin.
    endpoint_margin = check.output_delay ? -*check.output_delay : (analysis_type == AnalysisType::kMin ? check.check_time : -check.check_time);
  } else {
    const auto clock = database.sdc().get_clock_map().find(end_path_state.get_clock_name());
    const double edge = clock == database.sdc().get_clock_map().end()
                            ? 0.0
                            : clock->second.edge(end_path_state.get_launch_clock_edge_index().value_or(launch_trans_type == TransType::kFall ? 1 : 0)).time;
    anchor = edge + capture_arrival;
  }
  return _query.roundTime(anchor + exception.get_delay() + endpoint_margin);
}

double PathChecker::multicycleAdjustment(const TimingPathState& path, AnalysisType analysis, const ResolvedTimingExceptions& exceptions,
                                         const PathCheck& check)
{
  double adjustment = 0.0;
  if (exceptions.setup_multicycle && exceptions.setup_multicycle->get_setup_multiplier()) {
    const auto& setup = *exceptions.setup_multicycle;
    adjustment += exceptionClockShift(path, check, setup.get_setup_use_end_clock(), *setup.get_setup_multiplier() - 1, true);
  }
  if (analysis == AnalysisType::kMin && exceptions.hold_multicycle && exceptions.hold_multicycle->get_hold_multiplier()) {
    const auto& hold = *exceptions.hold_multicycle;
    adjustment -= exceptionClockShift(path, check, hold.get_hold_use_end_clock(), *hold.get_hold_multiplier(), false);
  }
  return adjustment;
}

double PathChecker::exceptionClockShift(const TimingPathState& path, const PathCheck& check, bool use_end_clock, int steps, bool setup)
{
  const auto name = use_end_clock ? check.capture_clock : path.get_clock_name();
  const auto found = _database.sdc().get_clock_map().find(std::string(name));
  if (found == _database.sdc().get_clock_map().end())
    return 0.;
  const auto& clock = found->second;
  const int64_t index = use_end_clock ? check.capture_edge_index.value_or(check.capture_transition == TransType::kFall ? 1 : 0)
                                      : path.get_launch_clock_edge_index().value_or(path.get_launch_clock_trans_type() == TransType::kFall ? 1 : 0);
  // Setup moves capture forward or launch backward. Hold moves in the
  // opposite direction. One cycle is the next edge of the same polarity,
  // which need not be one waveform period for a multi-pulse clock.
  const bool forward = use_end_clock == setup;
  const int64_t target = index + (forward ? 2LL : -2LL) * steps;
  const int64_t count = clock.edgeCount();
  const int64_t wrapped = (target % count + count) % count;
  const int64_t periods = (target - wrapped) / count;
  const double delta = clock.edge(wrapped).time + periods * clock.get_period() - clock.edge(index).time;
  return forward ? delta : -delta;
}

double PathChecker::calcPathSlack(const TimingPathState& end_path_state, double required_time, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return _query.roundTime(end_path_state.get_arrival() - required_time);
  }
  return _query.roundTime(required_time - end_path_state.get_arrival());
}

}  // namespace ista
