// SPDX-License-Identifier: MulanPSL-2.0
#include "MinTimingDataExporter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "PinCapacitanceCalculator.hpp"
#include "TimingData.hpp"
#include "graph/TimingStateQuery.hpp"
#include "timing_checker/PathChecker.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingCheckArc.hpp"
#include "network/Instance.hpp"
#include "network/Pin.hpp"

namespace ista {
namespace {

using MinCheckKey = std::tuple<std::string_view, std::string_view, int32_t, int32_t, int32_t>;

int32_t transitionCode(TransType transition)
{
  return transition == TransType::kRise ? 1 : transition == TransType::kFall ? -1 : 0;
}

TimingMinQualification excluded(TimingMinQualificationReason reason)
{
  return {{false, false}, {reason, reason}};
}

bool hasMinCaptureState(Database& database, const std::string& pin_name, TransType transition)
{
  const auto point = database.graph().points().find(pin_name);
  if (point == database.graph().points().end())
    return false;
  for (const auto& [clock_name, state] : point->second.get_clock_state_map()) {
    if (state.is_source_path_only || !database.sdc().get_clock_map().contains(clock_name))
      continue;
    if (state.is_structural
        && !clockPinValues(database, pin_name, clock_name, AnalysisType::kMin, transition).propagated)
      return true;
    const auto value = state.arrival(AnalysisType::kMin, transition);
    if (value.has_value() && std::isfinite(*value))
      return true;
  }
  return false;
}

TimingMinQualification qualifyMinOutput(Database& database, const std::string& pin_name)
{
  auto result = excluded(TimingMinQualificationReason::kNoOutputConstraint);
  const auto port = database.sdc().get_port_constraint_map().find(pin_name);
  if (port == database.sdc().get_port_constraint_map().end())
    return result;
  for (std::size_t edge = 0; edge < 2; ++edge) {
    const auto transition = edge == 0 ? TransType::kRise : TransType::kFall;
    for (const auto* delay : port->second.get_output_delays(AnalysisType::kMin, transition)) {
      if (!database.sdc().get_clock_map().contains(delay->get_clock_name())) {
        result.reason[edge] = TimingMinQualificationReason::kUnsupportedConstraint;
        continue;
      }
      result.valid[edge] = true;
      result.reason[edge] = TimingMinQualificationReason::kValid;
    }
  }
  return result;
}

bool hasCheckTable(const LibertyArcData& arc, std::size_t edge)
{
  return edge == 0 ? !arc.rise_check.values.empty() : !arc.fall_check.values.empty();
}

TimingMinQualification qualifyMinCheck(Database& database, const TimingEndpointArcData& endpoint,
                                       const TimingData& snapshot, const std::set<MinCheckKey>& active_checks)
{
  const auto check_type = static_cast<TimingCheckType>(endpoint.check_type);
  if (check_type != TimingCheckType::kHold && check_type != TimingCheckType::kRemoval)
    return excluded(TimingMinQualificationReason::kNotMinCheck);
  if (!hasMinCaptureState(database, endpoint.source_pin, endpoint.timing_type == 1 ? TransType::kRise : TransType::kFall))
    return excluded(TimingMinQualificationReason::kNoCaptureClockState);
  auto result = excluded(TimingMinQualificationReason::kInactiveCheck);
  const auto cell = std::find_if(snapshot.lib_cells.begin(), snapshot.lib_cells.end(), [&](const auto& value) {
    return value.name == endpoint.library_cell;
  });
  if (cell == snapshot.lib_cells.end() || endpoint.library_arc_id < 0
      || static_cast<std::size_t>(endpoint.library_arc_id) >= cell->arcs.size())
    return excluded(TimingMinQualificationReason::kInactiveCheck);
  const auto& arc = cell->arcs[endpoint.library_arc_id];
  for (std::size_t edge = 0; edge < 2; ++edge) {
    if (!database.graph().simValues().allowsTransition(endpoint.sink_pin, edge == 0 ? TransType::kRise : TransType::kFall)) {
      result.reason[edge] = TimingMinQualificationReason::kDisabledTransition;
      continue;
    }
    auto active = [&](int32_t capture_transition) {
      return active_checks.contains({endpoint.source_pin, endpoint.sink_pin, endpoint.check_type,
                                     edge == 0 ? 1 : -1, capture_transition});
    };
    const bool legal_context = endpoint.timing_type == 0 ? active(1) || active(-1) : active(endpoint.timing_type);
    result.valid[edge] = hasCheckTable(arc, edge) && legal_context;
    if (result.valid[edge])
      result.reason[edge] = TimingMinQualificationReason::kValid;
  }
  return result;
}

void exportMinTiming(TimingData& snapshot, Database& database, const Config& config, bool structural_model)
{
  DelayCalculator calculator(database, config);
  PinCapacitanceCalculator pin_capacitance(database);
  TimingStateQuery query(database);
  PathChecker checker(database, calculator);
  std::unordered_map<std::string, TimingPinData*> pins;
  pins.reserve(snapshot.pins.size());
  for (auto& pin : snapshot.pins) {
    pin.min_qualification = excluded(TimingMinQualificationReason::kUnconstrainedGraphTerminal);
    pin.min_rise_aat_ps = pin.min_fall_aat_ps = 9.0e7;
    pin.min_rise_rat_ps = pin.min_fall_rat_ps = -9.0e7;
    pins.emplace(pin.name, &pin);
    if (pin.is_port && (pin.direction == static_cast<int32_t>(PinDirection::kOutput)
                        || pin.direction == static_cast<int32_t>(PinDirection::kInout))) {
      // HoldRC adds these to endpoint pin caps before summing the net load.
      // Export only this output port's external load, never the other sinks.
      pin.min_output_rise_load_pf = pin_capacitance.pinCapacitance(pin.name, AnalysisType::kMin, TransType::kRise) * snapshot.capacitance_unit_pf;
      pin.min_output_fall_load_pf = pin_capacitance.pinCapacitance(pin.name, AnalysisType::kMin, TransType::kFall) * snapshot.capacitance_unit_pf;
    }
  }

  std::unordered_set<std::string> start_points(snapshot.start_points.begin(), snapshot.start_points.end());
  std::unordered_set<std::string> endpoints(snapshot.end_points.begin(), snapshot.end_points.end());
  std::set<std::tuple<std::string, std::string, std::string, int32_t, int32_t, int32_t>> launch_keys;
  auto exportPoint = [&](const std::string& name, TimingPoint& point) {
    const auto min_it = point.get_path_state_map().find(AnalysisType::kMin);
    if (min_it == point.get_path_state_map().end())
      return;
    for (auto& [source_type, transitions] : min_it->second) {
      for (auto& [transition, paths] : transitions) {
        if (transition == TransType::kNone)
          continue;
        const std::size_t edge = transition == TransType::kRise ? 0 : 1;
        for (auto& [tag, path] : paths) {
          if (!std::isfinite(path.get_arrival()))
            continue;
          auto pin = pins.find(name);
          if (pin != pins.end()) {
            const double arrival = path.get_arrival() * snapshot.time_unit_ps;
            const double slew = path.get_slew() * snapshot.time_unit_ps;
            double& pin_arrival = edge == 0 ? pin->second->min_rise_aat_ps : pin->second->min_fall_aat_ps;
            double& pin_slew = edge == 0 ? pin->second->min_rise_slew_ps : pin->second->min_fall_slew_ps;
            if (!pin->second->min_path_valid[edge] || arrival < pin_arrival) {
              pin->second->min_path_valid[edge] = true;
              pin_arrival = arrival;
              pin_slew = slew;
            }
          }
          if (start_points.contains(name)) {
            const auto key = std::make_tuple(name, path.get_clock_name(), path.get_path_state_tag(), static_cast<int32_t>(source_type),
                                              transitionCode(transition), transitionCode(path.get_launch_clock_trans_type()));
            if (launch_keys.insert(key).second) {
              TimingMinLaunchData launch;
              launch.start_point = name;
              launch.launch_clock = path.get_clock_name();
              launch.launch_pin = path.get_crpr_clock_pin();
              launch.path_tag = path.get_path_state_tag();
              launch.source_type = static_cast<int32_t>(source_type);
              launch.data_transition = transitionCode(transition);
              launch.launch_transition = transitionCode(path.get_launch_clock_trans_type());
              launch.arrival_ps = path.get_arrival() * snapshot.time_unit_ps;
              launch.slew_ps = path.get_slew() * snapshot.time_unit_ps;
              launch.launch_time_ps = path.get_launch_time() * snapshot.time_unit_ps;
              if (!launch.launch_pin.empty())
                launch.launch_slew_ps = query.getClockSlew(launch.launch_pin, launch.launch_clock, AnalysisType::kMin,
                                                          path.get_launch_clock_trans_type(), path.get_launch_clock_trans_type())
                                       * snapshot.time_unit_ps;
              snapshot.min_launches.push_back(std::move(launch));
            }
          }
          if (!endpoints.contains(name))
            continue;
          const auto checked = checker.checkPathCandidates(path, name, AnalysisType::kMin);
          if (checked.empty())
            continue;
          // Keep every legal capture/check candidate. PathChecker's public
          // checkPath() remains the winner reduction used by setup; min/hold
          // consumers must retain the context before endpoint reduction.
          for (const auto& candidate : checked) {
            const auto& check = candidate.check();
            TimingMinContextData context;
            context.start_point = path.get_start_point();
            context.end_point = name;
            context.launch_clock = path.get_clock_name();
            context.launch_pin = path.get_crpr_clock_pin();
            context.capture_clock = check.capture_clock;
            context.capture_pin = check.capture_pin;
            context.path_tag = path.get_path_state_tag();
            context.source_type = static_cast<int32_t>(source_type);
            context.check_type = static_cast<int32_t>(check.type);
            context.data_transition = transitionCode(path.get_trans_type());
            context.launch_transition = transitionCode(path.get_launch_clock_trans_type());
            context.capture_transition = transitionCode(check.capture_transition);
            context.capture_pin_transition = transitionCode(check.capture_pin_transition);
            context.capture_edge_index = check.capture_edge_index.has_value() ? static_cast<int32_t>(*check.capture_edge_index) : -1;
            context.selected_check_arc_idx = check.selected_check_arc_idx;
            context.arrival_ps = path.get_arrival() * snapshot.time_unit_ps;
            context.required_ps = candidate.required() * snapshot.time_unit_ps;
            context.normal_required_ps = check.required * snapshot.time_unit_ps;
            context.data_slew_ps = path.get_slew() * snapshot.time_unit_ps;
            context.launch_time_ps = path.get_launch_time() * snapshot.time_unit_ps;
            context.check_time_ps = check.check_time * snapshot.time_unit_ps;
            context.uncertainty_ps = check.uncertainty * snapshot.time_unit_ps;
            context.cppr_ps = check.reconvergence * snapshot.time_unit_ps;
            if (!check.capture_pin.empty())
              context.capture_slew_ps = query.getClockSlew(check.capture_pin, check.capture_clock, AnalysisType::kMin,
                                                          check.capture_pin_transition, check.capture_transition)
                                        * snapshot.time_unit_ps;
            if (!context.launch_pin.empty())
              context.launch_clock_slew_ps = query.getClockSlew(context.launch_pin, context.launch_clock, AnalysisType::kMin,
                                                                path.get_launch_clock_trans_type(), path.get_launch_clock_trans_type())
                                             * snapshot.time_unit_ps;
            // capture_time already contains the scheduled edge, ideal clock
            // latency, and the signed reconvergence term. Keep the numeric
            // edge component zero here; capture_edge_index preserves its
            // identity without counting the edge twice in the consumer.
            context.capture_edge_ps = 0.0;
            context.capture_arrival_ps = (check.capture_time + check.reconvergence) * snapshot.time_unit_ps;
            if (structural_model && context.check_type != 0) {
              // Only the required-time rule is model data. Its live check is
              // evaluated later from runtime data/clock slew, so no LUT value
              // queried at the placeholder slew is a numeric min reference.
              context.required_ps -= context.check_time_ps;
              context.normal_required_ps -= context.check_time_ps;
              context.check_time_ps = 0.0;
              context.selected_check_arc_idx = -1;
            }
            const double candidate_slack = context.arrival_ps - context.required_ps;
            snapshot.min_contexts.push_back(std::move(context));
            auto endpoint_pin = pins.find(name);
            if (endpoint_pin != pins.end()) {
              endpoint_pin->second->min_qualification.valid[edge] = true;
              endpoint_pin->second->min_qualification.reason[edge] = TimingMinQualificationReason::kValid;
              double& arrival = edge == 0 ? endpoint_pin->second->min_rise_aat_ps : endpoint_pin->second->min_fall_aat_ps;
              double& required = edge == 0 ? endpoint_pin->second->min_rise_rat_ps : endpoint_pin->second->min_fall_rat_ps;
              if (candidate_slack < arrival - required) {
                arrival = path.get_arrival() * snapshot.time_unit_ps;
                required = candidate.required() * snapshot.time_unit_ps;
              }
            }
          }
        }
      }
    }
  };

  for (auto& [name, point] : database.graph().points())
    exportPoint(name, point);
  for (auto& [name, point] : database.graph().driverPoints())
    if (!database.graph().points().contains(name))
      exportPoint(name, point);

  // Contexts are now complete and immutable: borrowed strings remain valid.
  // A LUT and capture clock alone do not qualify a check whose min paths were
  // all excluded by exceptions or clock relationships.
  std::set<MinCheckKey> active_checks;
  for (const auto& context : snapshot.min_contexts)
    if (context.check_type != 0)
      active_checks.emplace(context.capture_pin, context.end_point, context.check_type,
                            context.data_transition, context.capture_pin_transition);
  for (auto& arc : snapshot.timing_check_arcs)
    arc.min_qualification = qualifyMinCheck(database, arc, snapshot, active_checks);
  for (auto& arc : snapshot.constraint_arcs)
    arc.min_qualification = qualifyMinCheck(database, arc, snapshot, active_checks);
  for (auto& pin : snapshot.pins) {
    if (pin.is_port) {
      auto output = qualifyMinOutput(database, pin.name);
      for (std::size_t edge = 0; edge < 2; ++edge) {
        // An output delay declares a check; it does not create a data launch.
        // Unclocked register outputs have no legal min context, just as paths
        // excluded by an exception have none.
        if (output.valid[edge] && !pin.min_qualification.valid[edge]) {
          output.valid[edge] = false;
          output.reason[edge] = TimingMinQualificationReason::kInactiveCheck;
        }
      }
      pin.min_qualification = output;
    }
    for (std::size_t edge = 0; edge < 2; ++edge) {
      if (!database.graph().simValues().allowsTransition(pin.name, edge == 0 ? TransType::kRise : TransType::kFall)) {
        pin.min_qualification.valid[edge] = false;
        pin.min_qualification.reason[edge] = TimingMinQualificationReason::kDisabledTransition;
      } else if (pin.min_qualification.valid[edge] && !pin.min_path_valid[edge]) {
        pin.min_qualification.valid[edge] = false;
        pin.min_qualification.reason[edge] = TimingMinQualificationReason::kNoMinPathState;
      }
    }
  }
  snapshot.diagnostics.push_back(structural_model ? "min_timing_export=structural_context_rules; no_numeric_min_reference"
                                                  : "min_timing_export=path_checker_all_candidate_contexts");
}

}  // namespace

void exportMinTimingData(TimingData& snapshot, Database& database, const Config& config)
{
  exportMinTiming(snapshot, database, config, false);
}

void exportMinTimingModel(TimingData& snapshot, Database& database, const Config& config)
{
  exportMinTiming(snapshot, database, config, true);
}

}  // namespace ista
