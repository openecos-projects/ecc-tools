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
 * @file SDCPathConstraints.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief SDC path exception and clock-group normalization.
 */

#include <algorithm>
#include <string>
#include <utility>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {

auto SdcSubsetEvaluator::evaluatePathException(const std::string& command, const std::vector<SdcValue>& args, SdcExceptionKind kind) -> void
{
  const auto parsed = parseOptions(
      command, args, {"-from", "-rise_from", "-fall_from", "-through", "-rise_through", "-fall_through", "-to", "-rise_to", "-fall_to", "-comment"},
      {"-rise", "-fall", "-setup", "-hold", "-start", "-end", "-reset_path", "-datapath_only", "-ignore_clock_latency", "-match_start_end"},
      {"-through", "-rise_through", "-fall_through"});
  if (!parsed) {
    return;
  }
  const auto& options = *parsed;
  const bool multicycle = kind == SdcExceptionKind::kMulticyclePath;
  const bool path_delay = kind == SdcExceptionKind::kMinDelay || kind == SdcExceptionKind::kMaxDelay;
  for (const auto& flag : options.flags) {
    const bool invalid = ((flag == "-start" || flag == "-end") && !multicycle) || ((flag == "-datapath_only" || flag == "-ignore_clock_latency") && !path_delay)
                         || ((flag == "-setup" || flag == "-hold") && path_delay) || (flag == "-match_start_end" && kind != SdcExceptionKind::kFalsePath);
    if (invalid) {
      reportIssue(SdcConstraintStatusCode::kUnsupported, command, "unsupported_option:" + flag);
      return;
    }
  }
  if (options.positional.size() != (kind == SdcExceptionKind::kFalsePath ? 0U : 1U)) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "invalid_positional_argument_count");
    return;
  }
  SdcPathException exception;
  exception.kind = kind;
  if (!readPathSelection(command, options, exception.path)) {
    return;
  }
  if (kind == SdcExceptionKind::kFalsePath && exception.path.from.objects.empty() && exception.path.through.empty() && exception.path.to.objects.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "path_selector_required");
    return;
  }
  exception.transition = OptionTransition(options);
  SelectBothUnlessOne(options, "-setup", "-hold", exception.setup, exception.hold);
  exception.reset_path = options.flags.contains("-reset_path");
  exception.match_start_end = options.flags.contains("-match_start_end");
  if (multicycle) {
    if (!ParseIntValue(ValueToString(options.positional.front()), exception.cycles)) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "invalid_cycle_multiplier");
      return;
    }
    if (options.flags.contains("-start") && options.flags.contains("-end")) {
      reportIssue(SdcConstraintStatusCode::kMalformed, command, "conflicting_start_end");
      return;
    }
    exception.reference_explicit = options.flags.contains("-start") || options.flags.contains("-end");
    exception.reference = options.flags.contains("-start") || (!exception.reference_explicit && !exception.setup) ? SdcMulticycleReference::kStart
                                                                                                                  : SdcMulticycleReference::kEnd;
  }
  if (path_delay) {
    if (!readNumber(command, options.positional.front(), exception.delay_ns)) {
      return;
    }
    if (!scaleNumber(command, exception.delay_ns, _time_unit_ns)) {
      return;
    }
    exception.setup = kind == SdcExceptionKind::kMaxDelay;
    exception.hold = kind == SdcExceptionKind::kMinDelay;
    exception.datapath_only = options.flags.contains("-datapath_only");
    exception.ignore_clock_latency = options.flags.contains("-ignore_clock_latency");
  }
  _data.path_exceptions.push_back(std::move(exception));
}

auto SdcSubsetEvaluator::evaluateClockGroups(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_clock_groups";
  const auto parsed = parseOptions(command, args, {"-name", "-group", "-comment"},
                                   {"-asynchronous", "-logically_exclusive", "-physically_exclusive", "-allow_paths"}, {"-group"});
  if (!parsed) {
    return;
  }
  const auto& options = *parsed;
  const auto kinds = static_cast<int>(options.flags.contains("-asynchronous")) + static_cast<int>(options.flags.contains("-logically_exclusive"))
                     + static_cast<int>(options.flags.contains("-physically_exclusive"));
  if (kinds != 1 || !options.values.contains("-group") || !options.positional.empty()) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "one_group_kind_and_nonempty_groups_required");
    return;
  }
  SdcClockGroup group;
  if (options.flags.contains("-logically_exclusive")) {
    group.kind = SdcClockGroupKind::kLogicallyExclusive;
  } else if (options.flags.contains("-physically_exclusive")) {
    group.kind = SdcClockGroupKind::kPhysicallyExclusive;
  }
  group.allow_paths = options.flags.contains("-allow_paths");
  if (group.allow_paths && group.kind != SdcClockGroupKind::kAsynchronous) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "allow_paths_requires_asynchronous");
    return;
  }
  if (const auto name = options.values.find("-name"); name != options.values.end()) {
    group.name = ValueToString(name->second);
  }
  for (const auto& [option, value] : options.ordered_values) {
    if (option == "-group") {
      std::vector<SdcObjectRef> clocks;
      if (!readRefs(command, value, SdcObjectKind::kClock, clocks)) {
        return;
      }
      group.groups.push_back(std::move(clocks));
    }
  }
  // One explicit group excludes its clocks from the complementary clock set.
  // Keep that form intact so generated clocks can be resolved by the consumer.
  _data.clock_groups.push_back(std::move(group));
}

}  // namespace icts::sdc_reader
