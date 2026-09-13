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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastSTAEvents.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Exact path eligibility and conservative, CRPR-disabled clock-event checks.
 */
#include "FastSTAEvents.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "FastSTAConstraints.hh"

namespace icts {
namespace {

constexpr auto kUnmatched = std::numeric_limits<std::size_t>::max();

auto delta(const std::unordered_map<FastStaNodeId, double>& deltas, FastStaNodeId node) -> double
{
  const auto found = deltas.find(node);
  return found == deltas.end() ? 0.0 : found->second;
}

auto startMatches(const FastStaContext& context, const SdcPathSelector& selector, const FastStaTimingPoint& point) -> bool
{
  if (selector.objects.empty()) {
    return FastStaConstraints::transitionMatches(selector.transition, point.launch_data_transition);
  }
  return std::ranges::any_of(selector.objects, [&](const auto& object) -> bool {
    const auto clock_matches = FastStaConstraints::matches(context, object, point.launch_clock_node_id, point.clock_name);
    const auto clock_object = object.kind == SdcObjectKind::kClock || clock_matches;
    const auto transition = clock_object ? point.launch_clock_transition : point.launch_data_transition;
    return FastStaConstraints::transitionMatches(selector.transition, transition)
           && (FastStaConstraints::matches(context, object, point.launch_node_id, point.clock_name) || clock_matches);
  });
}

auto exceptionMatches(const FastStaContext& context, const SdcPathException& exception, std::size_t index, const FastStaTimingPoint& point,
                      FastStaNodeId endpoint, FastStaTransition transition, const FastStaTimingPoint& capture, bool setup) -> bool
{
  const auto& selector = exception.path.to;
  const auto endpoint_matches = selector.objects.empty()
                                    ? FastStaConstraints::transitionMatches(selector.transition, transition)
                                    : std::ranges::any_of(selector.objects, [&](const auto& object) -> bool {
                                        const auto edge = object.kind == SdcObjectKind::kClock ? capture.launch_clock_transition : transition;
                                        return FastStaConstraints::transitionMatches(selector.transition, edge)
                                               && FastStaConstraints::matches(context, object, endpoint, capture.clock_name);
                                      });
  return (setup ? exception.setup : exception.hold) && FastStaConstraints::transitionMatches(exception.transition, transition)
         && index < point.exception_progress.size() && point.exception_progress.at(index) == exception.path.through.size() && endpoint_matches;
}

auto groupContains(const FastStaContext& context, const std::vector<SdcObjectRef>& group, const std::string& clock) -> bool
{
  return std::ranges::any_of(group, [&](const auto& object) -> bool { return FastStaConstraints::matches(context, object, kInvalidFastStaNodeId, clock); });
}

auto excludedClocks(const FastStaContext& context, const std::string& launch, const std::string& capture) -> bool
{
  for (const auto& groups : context.constraints.clock_groups) {
    if (groups.allow_paths) {
      continue;
    }
    std::optional<std::size_t> launch_group;
    std::optional<std::size_t> capture_group;
    for (std::size_t index = 0U; index < groups.groups.size(); ++index) {
      if (groupContains(context, groups.groups.at(index), launch)) {
        launch_group = index;
      }
      if (groupContains(context, groups.groups.at(index), capture)) {
        capture_group = index;
      }
    }
    if ((launch_group && capture_group && launch_group != capture_group)
        || (groups.groups.size() == 1U && launch_group.has_value() != capture_group.has_value())) {
      return true;
    }
  }
  return false;
}

// A Euclidean clock-period lattice avoids enumerating the common hyperperiod.
// Scale-relative machine precision handles floating-point cancellation, not a
// physical timing guard band; the final signed slack is never clamped.
auto periodLattice(double launch, double capture) -> double
{
  auto larger = std::max(launch, capture);
  auto smaller = std::min(launch, capture);
  const auto cancellation = 16.0 * std::numeric_limits<double>::epsilon() * larger;
  while (smaller > cancellation) {
    const auto remainder = std::fmod(larger, smaller);
    larger = smaller;
    smaller = std::min(remainder, std::abs(smaller - remainder));
  }
  return larger;
}

auto uncertainty(const FastStaContext& context, const FastStaTimingPoint& data, const FastStaTimingPoint& capture, const FastStaTimingCheck& check, bool setup)
    -> double
{
  double value = 0.0;
  for (const auto& constraint : context.constraints.clock_uncertainties) {
    if (!(setup ? constraint.setup : constraint.hold)) {
      continue;
    }
    const auto endpoint_matches = constraint.objects.empty() || std::ranges::any_of(constraint.objects, [&](const auto& object) -> bool {
                                    return FastStaConstraints::matches(context, object, check.clock_node_id, capture.clock_name);
                                  });
    if (endpoint_matches && FastStaConstraints::matches(context, constraint.from, data.launch_clock_node_id, data.launch_clock_transition, data.clock_name)
        && FastStaConstraints::matches(context, constraint.to, check.clock_node_id, capture.launch_clock_transition, capture.clock_name)) {
      value = constraint.value_ns;
    }
  }
  return value;
}

}  // namespace

auto FastStaEvents::startPath(const FastStaContext& context, FastStaTimingPoint& point, FastStaTransition transition) -> void
{
  point.launch_data_transition = transition;
  point.exception_progress.assign(context.constraints.path_exceptions.size(), kUnmatched);
  for (std::size_t index = 0U; index < context.constraints.path_exceptions.size(); ++index) {
    if (startMatches(context, context.constraints.path_exceptions.at(index).path.from, point)) {
      point.exception_progress.at(index) = 0U;
    }
  }
  advancePath(context, point, point.launch_node_id, transition);
}

auto FastStaEvents::advancePath(const FastStaContext& context, FastStaTimingPoint& point, FastStaNodeId node_id, FastStaTransition transition) -> void
{
  for (std::size_t index = 0U; index < point.exception_progress.size(); ++index) {
    auto& progress = point.exception_progress.at(index);
    const auto& through = context.constraints.path_exceptions.at(index).path.through;
    if (progress < through.size() && FastStaConstraints::matches(context, through.at(progress), node_id, transition, point.clock_name)) {
      ++progress;
    }
  }
}

auto FastStaEvents::sameTag(const FastStaContext& context, const FastStaTimingPoint& lhs, const FastStaTimingPoint& rhs, bool early) -> bool
{
  if (lhs.clock_name != rhs.clock_name || lhs.launch_clock_transition != rhs.launch_clock_transition || lhs.exception_progress != rhs.exception_progress) {
    return false;
  }
  if (lhs.launch_clock_node_id != rhs.launch_clock_node_id || lhs.launch_clock_arrival_ns != rhs.launch_clock_arrival_ns) {
    // Absolute arrival order can reverse after subtracting launch insertion.
    // Keep each origin until every potentially applicable path-only check is evaluated.
    for (std::size_t index = 0U; index < lhs.exception_progress.size(); ++index) {
      const auto& exception = context.constraints.path_exceptions.at(index);
      const auto path_delay
          = early ? exception.kind == SdcExceptionKind::kMinDelay && exception.hold : exception.kind == SdcExceptionKind::kMaxDelay && exception.setup;
      if (lhs.exception_progress.at(index) != kUnmatched && path_delay && (exception.datapath_only || exception.ignore_clock_latency)) {
        return false;
      }
    }
  }
  return true;
}

auto FastStaEvents::relation(const FastStaContext& context, const FastStaTimingCheck& check, FastStaTransition transition, const FastStaTimingPoint& data,
                             const FastStaTimingPoint& capture, double requirement_ns, const std::unordered_map<FastStaNodeId, double>& clock_deltas)
    -> std::optional<FastStaTimingRelationFact>
{
  const auto setup = check.kind == FastStaTimingCheckKind::kSetup;
  if (excludedClocks(context, data.clock_name, capture.clock_name)) {
    return std::nullopt;
  }
  const SdcPathException* setup_multicycle = nullptr;
  const SdcPathException* hold_multicycle = nullptr;
  const SdcPathException* path_delay = nullptr;
  std::optional<std::size_t> setup_multicycle_index;
  std::optional<std::size_t> hold_multicycle_index;
  std::optional<std::size_t> path_delay_index;
  for (std::size_t index = 0U; index < context.constraints.path_exceptions.size(); ++index) {
    const auto& exception = context.constraints.path_exceptions.at(index);
    const auto applies_setup = exceptionMatches(context, exception, index, data, check.data_node_id, transition, capture, true);
    const auto applies_hold = exceptionMatches(context, exception, index, data, check.data_node_id, transition, capture, false);
    if (exception.kind == SdcExceptionKind::kFalsePath && (setup ? applies_setup : applies_hold)) {
      return std::nullopt;
    }
    if (exception.kind == SdcExceptionKind::kMulticyclePath) {
      if (applies_setup) {
        setup_multicycle = &exception;
        setup_multicycle_index = index;
      }
      if (applies_hold) {
        hold_multicycle = &exception;
        hold_multicycle_index = index;
      }
    }
    if ((setup && exception.kind == SdcExceptionKind::kMaxDelay && applies_setup)
        || (!setup && exception.kind == SdcExceptionKind::kMinDelay && applies_hold)) {
      path_delay = &exception;
      path_delay_index = index;
    }
  }
  const auto launch_period = FastStaConstraints::period(context, data.clock_name);
  const auto capture_period = FastStaConstraints::period(context, capture.clock_name);
  const auto launch_phase = FastStaConstraints::phase(context, data.clock_name, data.launch_clock_transition);
  const auto capture_phase = FastStaConstraints::phase(context, capture.clock_name, capture.launch_clock_transition);
  const auto lattice = periodLattice(launch_period, capture_period);
  auto separation = std::fmod(capture_phase - launch_phase, lattice);
  if (separation <= 16.0 * std::numeric_limits<double>::epsilon() * lattice) {
    separation += lattice;
  }
  if (!setup) {
    separation -= lattice;
  }
  // Select the earliest non-negative pair on both actual event trains.  This
  // retains the launch-cycle shift for generated and inverted checks, instead
  // of reporting equivalent slack at a fictitious negative capture event.
  auto capture_cycle = std::max(0.0, std::ceil((launch_phase + separation - capture_phase) / capture_period - 16.0 * std::numeric_limits<double>::epsilon()));
  auto capture_event = capture_phase + capture_cycle * capture_period;
  auto launch_event = capture_event - separation;
  const auto maximum_pairings = std::max(1.0, std::round(launch_period / lattice));
  for (std::size_t pairing = 0U; static_cast<double>(pairing) < maximum_pairings; ++pairing) {
    const auto launch_cycles = (launch_event - launch_phase) / launch_period;
    if (std::abs(launch_cycles - std::round(launch_cycles)) <= 32.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(launch_cycles))) {
      break;
    }
    capture_event += capture_period;
    launch_event = capture_event - separation;
  }
  const auto reference_period
      = [&](const SdcPathException& exception) -> double { return exception.reference == SdcMulticycleReference::kStart ? launch_period : capture_period; };
  if (setup_multicycle != nullptr) {
    separation += static_cast<double>(setup_multicycle->cycles - 1) * reference_period(*setup_multicycle);
  }
  if (!setup && hold_multicycle != nullptr) {
    // A setup-and-hold declaration carries the setup multiplier; its hold
    // multiplier is zero. Only a hold-only command shifts that hold edge back.
    const auto hold_cycles = hold_multicycle->setup ? 0 : hold_multicycle->cycles;
    separation -= static_cast<double>(hold_cycles) * reference_period(*hold_multicycle);
  }
  auto arrival = data.arrival_ns + launch_event - launch_phase;
  auto required = launch_event + separation + capture.arrival_ns + delta(clock_deltas, check.clock_node_id) + (setup ? -requirement_ns : requirement_ns);
  const auto margin = uncertainty(context, data, capture, check, setup);
  required += setup ? -margin : margin;
  if (path_delay != nullptr) {
    arrival = data.arrival_ns - launch_phase;
    required = path_delay->delay_ns + capture.arrival_ns + delta(clock_deltas, check.clock_node_id) + (setup ? -requirement_ns : requirement_ns);
    if (path_delay->datapath_only || path_delay->ignore_clock_latency) {
      arrival -= data.launch_clock_arrival_ns;
      required = path_delay->delay_ns;
    }
  }
  const auto& launch_name = context.nodes.at(data.launch_node_id).name;
  const auto& endpoint_name = context.nodes.at(check.data_node_id).name;
  const auto edge_name = [](FastStaTransition edge) -> const char* { return edge == FastStaTransition::kRise ? "rise" : "fall"; };
  auto identity = data.clock_name + "|" + capture.clock_name + "|" + launch_name + "|" + endpoint_name + "|" + (check.clock_gating ? "gating_" : "")
                  + (setup ? "setup" : "hold") + "|" + edge_name(transition) + "|" + edge_name(data.launch_clock_transition) + "|"
                  + edge_name(capture.launch_clock_transition);
  const auto append_exception_index = [&identity](std::string_view label, const std::optional<std::size_t>& index) -> void {
    identity += "|";
    identity += label;
    identity += index.has_value() ? std::to_string(*index) : "none";
  };
  append_exception_index("mc_setup=", setup_multicycle_index);
  append_exception_index("mc_hold=", hold_multicycle_index);
  append_exception_index("delay=", path_delay_index);
  return FastStaTimingRelationFact{.relation_id = std::move(identity),
                                   .launch_pin_name = launch_name,
                                   .capture_pin_name = endpoint_name,
                                   .launch_node_id = data.launch_node_id,
                                   .capture_node_id = check.data_node_id,
                                   .launch_clock_node_id = data.launch_clock_node_id,
                                   .capture_clock_node_id = check.clock_node_id,
                                   .check = check.kind,
                                   .data_transition = transition,
                                   .launch_clock_transition = data.launch_clock_transition,
                                   .capture_clock_transition = capture.launch_clock_transition,
                                   .slack_ns = setup ? required - arrival : arrival - required,
                                   .uncertainty = {.numerical_ns = 1.0e-12},
                                   .launch_clock_name = data.clock_name,
                                   .capture_clock_name = capture.clock_name,
                                   .arrival_ns = arrival,
                                   .required_ns = required,
                                   .requirement_ns = requirement_ns,
                                   .clock_gating = check.clock_gating};
}

}  // namespace icts
