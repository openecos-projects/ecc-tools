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
 * @file SDCClockConstraints.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief SDC latency, uncertainty, slew, and external I/O timing facts.
 */

#include <algorithm>
#include <string>
#include <utility>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {

auto SdcSubsetEvaluator::evaluateClockLatency(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_clock_latency";
  const auto parsed = parseOptions(command, args, {"-clock"}, {"-rise", "-fall", "-min", "-max", "-source", "-early", "-late"});
  if (!parsed) {
    return;
  }
  const auto& options = *parsed;
  SdcClockLatency latency;
  if (!readScalarObjects(command, options, SdcObjectKind::kUnknown, latency.value_ns, latency.objects)) {
    return;
  }
  if (const auto clock = options.values.find("-clock");
      clock != options.values.end() && !readRefs(command, clock->second, SdcObjectKind::kClock, latency.clocks)) {
    return;
  }
  latency.source = options.flags.contains("-source");
  if (!latency.source && (options.flags.contains("-early") || options.flags.contains("-late"))) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "early_late_requires_source");
    return;
  }
  if (!scaleNumber(command, latency.value_ns, _time_unit_ns)) {
    return;
  }
  latency.transition = OptionTransition(options);
  SelectBothUnlessOne(options, "-min", "-max", latency.min, latency.max);
  SelectBothUnlessOne(options, "-early", "-late", latency.early, latency.late);
  _data.clock_latencies.push_back(std::move(latency));
}

auto SdcSubsetEvaluator::evaluateClockUncertainty(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_clock_uncertainty";
  const auto parsed = parseOptions(command, args, {"-from", "-rise_from", "-fall_from", "-to", "-rise_to", "-fall_to"}, {"-rise", "-fall", "-setup", "-hold"});
  if (!parsed) {
    return;
  }
  const auto& options = *parsed;
  SdcClockUncertainty uncertainty;
  bool from_seen = false;
  bool to_seen = false;
  for (const auto& [option, value] : options.ordered_values) {
    const bool from = option == "-from" || option.ends_with("_from");
    if ((from && from_seen) || (!from && to_seen)) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "conflicting_selector:" + option);
      return;
    }
    auto& selector = from ? uncertainty.from : uncertainty.to;
    selector.transition = SelectorTransition(option);
    if (!readRefs(command, value, SdcObjectKind::kClock, selector.objects)) {
      return;
    }
    (from ? from_seen : to_seen) = true;
  }
  if (from_seen != to_seen) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "from_and_to_required_together");
    return;
  }
  if (from_seen) {
    if (options.positional.size() != 1U || !readNumber(command, options.positional.front(), uncertainty.value_ns)) {
      if (options.positional.size() != 1U) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "uncertainty_value_required");
      }
      return;
    }
    if (options.flags.contains("-rise") || options.flags.contains("-fall")) {
      if (uncertainty.to.transition != SdcTransition::kBoth) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "conflicting_to_transition");
        return;
      }
      uncertainty.to.transition = OptionTransition(options);
    }
  } else {
    if (options.flags.contains("-rise") || options.flags.contains("-fall")) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "single_clock_uncertainty_has_no_edge_selector");
      return;
    }
    if (!readScalarObjects(command, options, SdcObjectKind::kUnknown, uncertainty.value_ns, uncertainty.objects)) {
      return;
    }
  }
  if (!scaleNumber(command, uncertainty.value_ns, _time_unit_ns)) {
    return;
  }
  SelectBothUnlessOne(options, "-setup", "-hold", uncertainty.setup, uncertainty.hold);
  _data.clock_uncertainties.push_back(std::move(uncertainty));
}

auto SdcSubsetEvaluator::evaluateClockTransition(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_clock_transition";
  const auto parsed = parseOptions(command, args, {}, {"-rise", "-fall", "-min", "-max"});
  if (!parsed) {
    return;
  }
  SdcClockTransition transition;
  if (!readScalarObjects(command, *parsed, SdcObjectKind::kClock, transition.value_ns, transition.clocks)) {
    return;
  }
  if (transition.value_ns < 0.0) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "negative_transition");
    return;
  }
  if (!scaleNumber(command, transition.value_ns, _time_unit_ns)) {
    return;
  }
  transition.transition = OptionTransition(*parsed);
  SelectBothUnlessOne(*parsed, "-min", "-max", transition.min, transition.max);
  _data.clock_transitions.push_back(std::move(transition));
}

auto SdcSubsetEvaluator::evaluateIODelay(const std::string& command, const std::vector<SdcValue>& args, bool input) -> void
{
  const auto parsed = parseOptions(command, args, {"-clock", "-reference_pin"},
                                   {"-rise", "-fall", "-min", "-max", "-clock_fall", "-add_delay", "-source_latency_included", "-network_latency_included"});
  if (!parsed) {
    return;
  }
  const auto& options = *parsed;
  SdcIODelay delay;
  if (!readScalarObjects(command, options, SdcObjectKind::kPort, delay.value_ns, delay.objects)) {
    return;
  }
  if (const auto clock = options.values.find("-clock");
      clock != options.values.end() && !readRefs(command, clock->second, SdcObjectKind::kClock, delay.clocks)) {
    return;
  }
  if (const auto pin = options.values.find("-reference_pin");
      pin != options.values.end() && !readRefs(command, pin->second, SdcObjectKind::kPin, delay.reference_pins)) {
    return;
  }
  if (delay.clocks.size() > 1U || delay.reference_pins.size() > 1U) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "single_reference_clock_and_pin_required");
    return;
  }
  if (options.flags.contains("-clock_fall") && delay.clocks.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "clock_fall_requires_clock");
    return;
  }
  if (std::ranges::any_of(delay.objects,
                          [](const auto& object) -> bool { return object.kind != SdcObjectKind::kPort && object.kind != SdcObjectKind::kPin; })) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "port_or_pin_objects_required");
    return;
  }
  if (!scaleNumber(command, delay.value_ns, _time_unit_ns)) {
    return;
  }
  delay.transition = OptionTransition(options);
  SelectBothUnlessOne(options, "-min", "-max", delay.min, delay.max);
  delay.clock_fall = options.flags.contains("-clock_fall");
  delay.add_delay = options.flags.contains("-add_delay");
  delay.source_latency_included = options.flags.contains("-source_latency_included");
  delay.network_latency_included = options.flags.contains("-network_latency_included");
  (input ? _data.input_delays : _data.output_delays).push_back(std::move(delay));
}

auto SdcSubsetEvaluator::evaluateInputTransition(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_input_transition";
  const auto parsed = parseOptions(command, args, {}, {"-rise", "-fall", "-min", "-max"});
  if (!parsed) {
    return;
  }
  SdcInputTransition transition;
  if (!readScalarObjects(command, *parsed, SdcObjectKind::kPort, transition.value_ns, transition.objects)) {
    return;
  }
  if (transition.value_ns < 0.0 || std::ranges::any_of(transition.objects, [](const auto& object) -> bool { return object.kind != SdcObjectKind::kPort; })) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "nonnegative_transition_and_ports_required");
    return;
  }
  if (!scaleNumber(command, transition.value_ns, _time_unit_ns)) {
    return;
  }
  transition.transition = OptionTransition(*parsed);
  SelectBothUnlessOne(*parsed, "-min", "-max", transition.min, transition.max);
  _data.input_transitions.push_back(std::move(transition));
}

auto SdcSubsetEvaluator::evaluateLoad(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_load";
  const auto parsed = parseOptions(command, args, {}, {"-rise", "-fall", "-min", "-max", "-pin_load", "-wire_load", "-subtract_pin_load"});
  if (!parsed) {
    return;
  }
  SdcLoad load;
  if (!readScalarObjects(command, *parsed, SdcObjectKind::kUnknown, load.value_pf, load.objects)) {
    return;
  }
  if (load.value_pf < 0.0 || (parsed->flags.contains("-pin_load") && parsed->flags.contains("-wire_load"))) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "invalid_load_value_or_conflicting_load_kind");
    return;
  }
  if (!scaleNumber(command, load.value_pf, _capacitance_unit_pf)) {
    return;
  }
  load.transition = OptionTransition(*parsed);
  SelectBothUnlessOne(*parsed, "-min", "-max", load.min, load.max);
  load.pin_load = parsed->flags.contains("-pin_load");
  load.wire_load = parsed->flags.contains("-wire_load");
  load.subtract_pin_load = parsed->flags.contains("-subtract_pin_load");
  _data.loads.push_back(std::move(load));
}

}  // namespace icts::sdc_reader
