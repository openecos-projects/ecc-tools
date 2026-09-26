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
 * @file SDCConstraintOptions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Validated SDC option and object-selector normalization.
 */

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {

auto SdcSubsetEvaluator::reportIssue(SdcConstraintStatusCode code, const std::string& command, const std::string& detail) -> void
{
  if (_data.status == SdcConstraintStatusCode::kOk) {
    _data.status = code;
  }
  _data.issues.push_back({code, command, detail});
  _data.diagnostics.emplace_back(command + ":" + detail);
}

auto SdcSubsetEvaluator::reportIgnored(const std::string& command, const std::string& detail) -> void
{
  _data.ignored.emplace_back(command + ":" + detail);
}

auto SdcSubsetEvaluator::parseOptions(const std::string& command, const std::vector<SdcValue>& args, std::initializer_list<std::string_view> value_options,
                                      std::initializer_list<std::string_view> flag_options, std::initializer_list<std::string_view> repeated_options)
    -> std::optional<SdcCommandOptions>
{
  SdcCommandOptions options;
  for (std::size_t index = 0U; index < args.size(); ++index) {
    const auto token = ValueToString(args[index]);
    if (std::ranges::find(value_options, token) != value_options.end()) {
      if (index + 1U == args.size() || IsOption(ValueToString(args[index + 1U]))) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "missing_value:" + token);
        return std::nullopt;
      }
      if (options.values.contains(token) && std::ranges::find(repeated_options, token) == repeated_options.end()) {
        reportIssue(SdcConstraintStatusCode::kMalformed, command, "duplicate_option:" + token);
        return std::nullopt;
      }
      options.values[token] = args[++index];
      options.ordered_values.emplace_back(token, args[index]);
    } else if (std::ranges::find(flag_options, token) != flag_options.end()) {
      options.flags.insert(token);
    } else if (IsOption(token)) {
      reportIssue(SdcConstraintStatusCode::kUnsupported, command, "unsupported_option:" + token);
      return std::nullopt;
    } else {
      options.positional.push_back(args[index]);
    }
  }
  return options;
}

auto SdcSubsetEvaluator::readRefs(const std::string& command, const SdcValue& value, SdcObjectKind default_kind, std::vector<SdcObjectRef>& refs) -> bool
{
  const auto before = refs.size();
  AppendRefsFromValue(refs, value, default_kind);
  if (before == refs.size()) {
    reportIssue(SdcConstraintStatusCode::kUnresolvedReference, command, "empty_object_collection");
    return false;
  }
  for (std::size_t index = before; index < refs.size(); ++index) {
    auto& ref = refs[index];
    if (ref.kind == SdcObjectKind::kUnknown && !ref.from_collection_cmd) {
      ref.kind = default_kind;
    }
    if (ref.pattern.empty()) {
      reportIssue(SdcConstraintStatusCode::kUnresolvedReference, command, "empty_object_name");
      return false;
    }
    if (default_kind == SdcObjectKind::kClock && ref.kind != SdcObjectKind::kClock) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "expected_clock_collection:" + ref.pattern);
      return false;
    }
    if (ref.kind == SdcObjectKind::kClock
        && std::ranges::none_of(_data.clocks, [&](const auto& clock) -> bool { return ObjectPatternMatches(ref.pattern, clock.clock_name); })) {
      reportIssue(SdcConstraintStatusCode::kUnresolvedReference, command, "unresolved_clock:" + ref.pattern);
      return false;
    }
  }
  return true;
}

auto SdcSubsetEvaluator::readNumber(const std::string& command, const SdcValue& value, double& number) -> bool
{
  if (!value.objects.empty() || !ParseDoubleValue(ValueToString(value), number)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "invalid_number:" + ValueToString(value));
    return false;
  }
  return true;
}

auto SdcSubsetEvaluator::scaleNumber(const std::string& command, double& number, double scale) -> bool
{
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    reportIssue(SdcConstraintStatusCode::kUnresolvedReference, command, "physical_unit_unavailable");
    return false;
  }
  number *= scale;
  if (!std::isfinite(number)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "scaled_value_overflow");
    return false;
  }
  return true;
}

auto SdcSubsetEvaluator::readScalarObjects(const std::string& command, const SdcCommandOptions& options, SdcObjectKind default_kind, double& number,
                                           std::vector<SdcObjectRef>& objects) -> bool
{
  if (options.positional.size() != 2U) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "value_and_objects_required");
    return false;
  }
  return readNumber(command, options.positional[0], number) && readRefs(command, options.positional[1], default_kind, objects);
}

}  // namespace icts::sdc_reader
