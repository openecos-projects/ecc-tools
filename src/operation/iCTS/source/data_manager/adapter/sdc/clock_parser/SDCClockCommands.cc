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
 * @file SDCClockCommands.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief SDC clock command handlers.
 */

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {

auto SdcSubsetEvaluator::evaluateSet(const std::vector<ParsedWord>& words, const std::vector<SdcValue>& args) -> SdcValue
{
  if (words.size() < 2U) {
    reportIssue(SdcConstraintStatusCode::kMalformed, "set", "variable_required");
    return {};
  }
  const auto variable_name = words[1].text;
  if (words.size() == 2U) {
    if (const auto iter = _variables.find(variable_name); iter != _variables.end()) {
      return iter->second;
    }
    reportIssue(SdcConstraintStatusCode::kUnresolvedReference, "set", "unresolved_variable:" + variable_name);
    return {};
  }
  if (words.size() == 3U) {
    auto value = args.front();
    _variables[variable_name] = value;
    return value;
  }
  std::vector<std::string> joined_values;
  joined_values.reserve(args.size());
  std::ranges::transform(args, std::back_inserter(joined_values), [](const SdcValue& value) -> std::string { return ValueToString(value); });
  auto value = MakeStringValue(JoinStrings(joined_values));
  _variables[variable_name] = value;
  return value;
}

auto SdcSubsetEvaluator::evaluateExpr(const std::vector<SdcValue>& args) -> SdcValue
{
  std::string expression;
  for (const auto& arg : args) {
    if (!expression.empty()) {
      expression += ' ';
    }
    expression += ValueToString(arg);
  }
  double expression_value = 0.0;
  if (!ArithmeticParser(expression).parse(expression_value)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, "expr", "unresolved_expression:" + expression);
    return {};
  }
  std::ostringstream stream;
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << expression_value;
  return MakeStringValue(stream.str());
}

auto SdcSubsetEvaluator::evaluateSetUnits(const std::vector<SdcValue>& args) -> void
{
  const auto parsed = parseOptions("set_units", args, {"-time", "-capacitance"}, {});
  if (!parsed) {
    return;
  }
  if (!parsed->positional.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, "set_units", "unexpected_positional_argument");
    return;
  }
  if (const auto time = parsed->values.find("-time"); time != parsed->values.end()) {
    const double scale = TimeUnitToNs(ValueToString(time->second));
    if (!(scale > 0.0)) {
      reportIssue(SdcConstraintStatusCode::kMalformed, "set_units", "invalid_time_unit:" + ValueToString(time->second));
      return;
    }
    _time_unit_ns = scale;
  }
  if (const auto cap = parsed->values.find("-capacitance"); cap != parsed->values.end()) {
    const double scale = CapacitanceUnitToPf(ValueToString(cap->second));
    if (!(scale > 0.0)) {
      reportIssue(SdcConstraintStatusCode::kMalformed, "set_units", "invalid_capacitance_unit:" + ValueToString(cap->second));
      return;
    }
    _capacitance_unit_pf = scale;
  }
}

auto SdcSubsetEvaluator::evaluateCollection(SdcObjectKind kind, const std::vector<SdcValue>& args) -> SdcValue
{
  std::string command = "get_nets";
  if (kind == SdcObjectKind::kClock) {
    command = "get_clocks";
  } else if (kind == SdcObjectKind::kPin) {
    command = "get_pins";
  } else if (kind == SdcObjectKind::kPort) {
    command = "get_ports";
  }
  const auto parsed = parseOptions(command, args, {}, {"-quiet"});
  if (!parsed) {
    return {};
  }
  std::vector<std::string> patterns;
  for (const auto& value : parsed->positional) {
    for (const auto& object : value.objects) {
      patterns.push_back(object.pattern);
    }
    for (const auto& text : value.strings) {
      const auto items = SplitListText(text);
      patterns.insert(patterns.end(), items.begin(), items.end());
    }
  }
  if (patterns.empty()) {
    reportIssue(SdcConstraintStatusCode::kUnresolvedReference, command, "empty_object_collection");
    return {};
  }
  auto value = MakeObjectValue(kind, patterns);
  if (kind == SdcObjectKind::kClock) {
    std::vector<SdcObjectRef> refs;
    if (!readRefs(command, value, kind, refs)) {
      return {};
    }
  }
  return value;
}

auto SdcSubsetEvaluator::evaluateGetClocks(const std::vector<SdcValue>& args) -> SdcValue
{
  if (args.empty()) {
    return evaluateAllClocks();
  }
  return evaluateCollection(SdcObjectKind::kClock, args);
}

auto SdcSubsetEvaluator::evaluateAllClocks() -> SdcValue
{
  std::vector<std::string> clock_names;
  clock_names.reserve(_data.clocks.size());
  for (const auto& clock : _data.clocks) {
    clock_names.emplace_back(clock.clock_name);
  }
  return MakeObjectValue(SdcObjectKind::kClock, clock_names);
}

auto SdcSubsetEvaluator::evaluateCreateClock(const std::vector<SdcValue>& args) -> SdcValue
{
  const std::string command = "create_clock";
  const auto parsed = parseOptions(command, args, {"-name", "-period", "-waveform", "-comment"}, {"-add"});
  if (!parsed) {
    return {};
  }
  const auto& options = *parsed;
  SdcClockDecl clock;
  clock.kind = SdcClockDecl::Kind::kPrimary;
  clock.add = options.flags.contains("-add");
  if (!options.values.contains("-period") || options.positional.size() > 1U) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "period_and_at_most_one_target_list_required");
    return {};
  }
  if (!readNumber(command, options.values.at("-period"), clock.period_ns)) {
    return {};
  }
  if (!scaleNumber(command, clock.period_ns, _time_unit_ns)) {
    return {};
  }
  if (!(clock.period_ns > 0.0)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "positive_period_required");
    return {};
  }
  clock.period_resolved = true;
  if (!options.positional.empty() && !readRefs(command, options.positional.front(), SdcObjectKind::kUnknown, clock.targets)) {
    return {};
  }
  if (const auto name = options.values.find("-name"); name != options.values.end()) {
    clock.clock_name = ValueToString(name->second);
  } else if (!clock.targets.empty() && !clock.add) {
    clock.clock_name = clock.targets.front().pattern;
  }
  if (clock.clock_name.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "clock_name_required");
    return {};
  }
  if (const auto waveform = options.values.find("-waveform"); waveform != options.values.end()) {
    clock.waveform_explicit = true;
    const auto edges = SplitListText(ValueToString(waveform->second));
    if (edges.empty() || edges.size() % 2U != 0U) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "waveform_requires_even_nonzero_edge_count");
      return {};
    }
    for (const auto& edge : edges) {
      double time = 0.0;
      if (!ParseDoubleValue(edge, time)) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "invalid_waveform_edge:" + edge);
        return {};
      }
      if (!scaleNumber(command, time, _time_unit_ns)) {
        return {};
      }
      if ((!clock.waveform_ns.empty() && time < clock.waveform_ns.back()) || time > clock.period_ns * 2.0) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "waveform_edges_must_increase_within_two_periods");
        return {};
      }
      clock.waveform_ns.push_back(time);
    }
  } else {
    clock.waveform_ns = {0.0, clock.period_ns / 2.0};
  }
  clock.waveform_resolved = true;
  clock.is_virtual = clock.targets.empty();
  const auto clock_name = clock.clock_name;
  storeClock(std::move(clock));
  return MakeObjectValue(SdcObjectKind::kClock, {clock_name});
}

auto SdcSubsetEvaluator::evaluateCreateGeneratedClock(const std::vector<SdcValue>& args) -> SdcValue
{
  const std::string command = "create_generated_clock";
  const auto parsed
      = parseOptions(command, args, {"-name", "-source", "-master_clock", "-divide_by", "-multiply_by", "-edges", "-edge_shift", "-duty_cycle", "-comment"},
                     {"-invert", "-combinational", "-add"});
  if (!parsed) {
    return {};
  }
  const auto& options = *parsed;
  SdcClockDecl clock;
  clock.kind = SdcClockDecl::Kind::kGenerated;
  clock.add = options.flags.contains("-add");
  clock.invert = options.flags.contains("-invert");
  clock.combinational = options.flags.contains("-combinational");
  clock.divide_by_explicit = options.values.contains("-divide_by");
  clock.multiply_by_explicit = options.values.contains("-multiply_by");
  if (options.positional.size() != 1U || !options.values.contains("-source")) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "source_and_target_list_required");
    return {};
  }
  if (!readRefs(command, options.positional.front(), SdcObjectKind::kUnknown, clock.targets)
      || !readRefs(command, options.values.at("-source"), SdcObjectKind::kUnknown, clock.generated_sources)) {
    return {};
  }
  if (clock.generated_sources.size() != 1U) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "single_generated_source_required");
    return {};
  }
  if (const auto name = options.values.find("-name"); name != options.values.end()) {
    clock.clock_name = ValueToString(name->second);
  } else if (!clock.add) {
    clock.clock_name = clock.targets.front().pattern;
  }
  if (clock.clock_name.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "clock_name_required");
    return {};
  }
  if (const auto master = options.values.find("-master_clock"); master != options.values.end()) {
    std::vector<SdcObjectRef> clocks;
    if (!readRefs(command, master->second, SdcObjectKind::kClock, clocks)) {
      return {};
    }
    std::vector<std::string> names;
    for (const auto& candidate : _data.clocks) {
      if (std::ranges::any_of(clocks, [&](const auto& ref) -> bool { return ObjectPatternMatches(ref.pattern, candidate.clock_name); })) {
        names.push_back(candidate.clock_name);
      }
    }
    if (names.size() != 1U) {
      reportIssue(SdcConstraintStatusCode::kUnresolvedReference, command, "ambiguous_master_clock");
      return {};
    }
    clock.master_clock_name = names.front();
  }
  if (clock.add && clock.master_clock_name.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "add_requires_master_clock");
    return {};
  }
  const bool edges = options.values.contains("-edges");
  const int transforms = static_cast<int>(clock.divide_by_explicit) + static_cast<int>(clock.multiply_by_explicit) + static_cast<int>(edges);
  if (transforms > 1 || (transforms == 0 && !clock.combinational)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "one_generated_clock_transform_required");
    return {};
  }
  if ((clock.divide_by_explicit && (!ParseIntValue(ValueToString(options.values.at("-divide_by")), clock.divide_by) || clock.divide_by < 1))
      || (clock.multiply_by_explicit && (!ParseIntValue(ValueToString(options.values.at("-multiply_by")), clock.multiply_by) || clock.multiply_by < 1))) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "positive_integer_clock_ratio_required");
    return {};
  }
  if (clock.combinational && (clock.divide_by != 1 || clock.multiply_by_explicit || edges)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "combinational_requires_divide_by_one");
    return {};
  }
  if (edges) {
    const auto edge_values = SplitListText(ValueToString(options.values.at("-edges")));
    if (edge_values.size() != 3U) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "three_generated_edges_required");
      return {};
    }
    for (const auto& value : edge_values) {
      int edge = 0;
      if (!ParseIntValue(value, edge) || edge < 1 || (!clock.generated_edges.empty() && edge <= clock.generated_edges.back())) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "increasing_positive_generated_edges_required");
        return {};
      }
      clock.generated_edges.push_back(edge);
    }
  }
  if (options.values.contains("-edge_shift")) {
    const auto shifts = SplitListText(ValueToString(options.values.at("-edge_shift")));
    if (!edges || shifts.size() != clock.generated_edges.size()) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "edge_shift_requires_matching_edges");
      return {};
    }
    for (const auto& value : shifts) {
      double shift = 0.0;
      if (!ParseDoubleValue(value, shift)) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "invalid_edge_shift:" + value);
        return {};
      }
      if (!scaleNumber(command, shift, _time_unit_ns)) {
        return {};
      }
      clock.generated_edge_shifts_ns.push_back(shift);
    }
  }
  if (clock.invert && edges) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "invert_requires_ratio_or_combinational_transform");
    return {};
  }
  if (options.values.contains("-duty_cycle")) {
    double duty_cycle = 0.0;
    if (!clock.multiply_by_explicit || !readNumber(command, options.values.at("-duty_cycle"), duty_cycle) || duty_cycle <= 0.0 || duty_cycle >= 100.0) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "duty_cycle_requires_multiplier_and_percent_between_zero_and_100");
      return {};
    }
    clock.duty_cycle_percent = duty_cycle;
  }
  const auto clock_name = clock.clock_name;
  storeClock(std::move(clock));
  return MakeObjectValue(SdcObjectKind::kClock, {clock_name});
}

auto SdcSubsetEvaluator::evaluateSetCaseAnalysis(const std::vector<SdcValue>& args) -> void
{
  if (args.size() != 2U) {
    reportIssue(SdcConstraintStatusCode::kMalformed, "set_case_analysis", "value_and_objects_required");
    return;
  }
  SdcCaseAnalysis analysis;
  if (!ParseIntValue(ValueToString(args.front()), analysis.value) || (analysis.value != 0 && analysis.value != 1)) {
    const auto value = ValueToString(args.front());
    reportIssue(value == "rise" || value == "fall" || value == "rising" || value == "falling" ? SdcConstraintStatusCode::kUnsupported
                                                                                              : SdcConstraintStatusCode::kMalformed,
                "set_case_analysis", "only_constant_zero_or_one_case_values_supported");
    return;
  }
  if (!readRefs("set_case_analysis", args[1], SdcObjectKind::kUnknown, analysis.objects)) {
    return;
  }
  _data.case_analyses.push_back(std::move(analysis));
}

namespace {

auto ObjectKindToSourcePrefix(SdcObjectKind kind) -> const char*
{
  switch (kind) {
    case SdcObjectKind::kPort:
      return "port:";
    case SdcObjectKind::kPin:
      return "pin:";
    case SdcObjectKind::kNet:
      return "net:";
    case SdcObjectKind::kClock:
      return "clock:";
    case SdcObjectKind::kUnknown:
      return "";
  }
  return "";
}

}  // namespace

auto PrimarySourceExpression(const SdcClockDecl& clock) -> std::string
{
  const auto& refs = clock.targets.empty() ? clock.generated_sources : clock.targets;
  if (refs.empty()) {
    return {};
  }
  return std::string(ObjectKindToSourcePrefix(refs.front().kind)) + refs.front().pattern;
}

}  // namespace icts::sdc_reader
