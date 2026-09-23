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
#include "TimingExceptionMatcher.hpp"

#include "Database.hpp"
#include "Utility.hpp"

namespace ipw {

namespace {

bool isRelevantToAnalysis(const TimingException& exception, AnalysisType analysis_type)
{
  switch (exception.get_type()) {
    case TimingExceptionType::kFalsePath:
      return analysis_type == AnalysisType::kMax ? exception.get_setup() : exception.get_hold();
    case TimingExceptionType::kMaxDelay:
      return analysis_type == AnalysisType::kMax;
    case TimingExceptionType::kMinDelay:
      return analysis_type == AnalysisType::kMin;
    case TimingExceptionType::kMulticycle:
      return analysis_type == AnalysisType::kMax ? exception.get_setup_multiplier().has_value()
                                                 : exception.get_setup_multiplier().has_value()
                                                       || exception.get_hold_multiplier().has_value();
  }
  return false;
}

int endpointSelectorSpecificity(Database& database, const std::set<std::string>& objects, std::string_view pin_name,
                                std::string_view clock_name, bool start, int concrete_score, int clock_score)
{
  if (objects.empty()) {
    return 0;
  }
  bool concrete = objects.contains(std::string(pin_name));
  const auto pin = database.get_pin_map().find(std::string(pin_name));
  if (pin != database.get_pin_map().end() && !pin->second.get_is_port()) {
    concrete = concrete || objects.contains(pin->second.get_instance_name());
    const auto instance = database.get_instance_map().find(pin->second.get_instance_name());
    concrete = concrete || (start && instance != database.get_instance_map().end()
                            && objects.contains(instance->second.get_clock_pin_name()));
  }
  if (concrete) {
    return concrete_score;
  }
  return objects.contains(std::string(clock_name)) ? clock_score : 0;
}

int getSpecificity(Database& database, const TimingException& exception, std::string_view start, std::string_view launch_clock,
                   std::string_view end, std::string_view capture_clock)
{
  // Keep these bits aligned with OpenSTA's endpoint/through priority order.
  int score = endpointSelectorSpecificity(database, exception.get_from_objects(), start, launch_clock, true, 1 << 6, 1 << 3)
              + endpointSelectorSpecificity(database, exception.get_to_objects(), end, capture_clock, false, 1 << 5, 1 << 2);
  if (!exception.get_through_list().empty()) {
    score += 1 << 4;
  }
  if (exception.get_type() == TimingExceptionType::kMulticycle) {
    score += exception.get_setup() && exception.get_hold() ? 1 : 2;
  }
  return score;
}

bool isTighterPathDelay(const TimingException& candidate, const TimingException* current, int candidate_specificity,
                        int current_specificity)
{
  if (current == nullptr || candidate_specificity != current_specificity) {
    return current == nullptr || candidate_specificity > current_specificity;
  }
  return candidate.get_type() == TimingExceptionType::kMaxDelay ? candidate.get_delay() < current->get_delay()
                                                                : candidate.get_delay() > current->get_delay();
}

bool isTighterMulticycle(const TimingException* current, int32_t candidate_multiplier, std::optional<int32_t> current_multiplier,
                         int candidate_specificity, int current_specificity)
{
  if (current == nullptr || candidate_specificity != current_specificity) {
    return current == nullptr || candidate_specificity > current_specificity;
  }
  return !current_multiplier.has_value() || candidate_multiplier < *current_multiplier;
}

}  // namespace

std::vector<int32_t> TimingExceptionMatcher::initState(Database& database, std::string_view start, std::string_view clock,
                                                       TransType start_trans_type, AnalysisType analysis_type)
{
  std::vector<int32_t> state_list;
  for (const TimingException& exception : database.get_timing_constraint().get_path_exception_list()) {
    bool applies = isRelevantToAnalysis(exception, analysis_type)
                   && matchesEndpointObjects(database, exception.get_from_objects(), start, clock, true);
    if (applies && exception.get_from_trans_type() != TransType::kNone) {
      const bool clock_selector = !clock.empty() && exception.get_from_objects().contains(std::string(clock));
      const TransType actual_trans_type = clock_selector ? PWUTIL.getLaunchClockTransition(database, start) : start_trans_type;
      applies = actual_trans_type == exception.get_from_trans_type();
    }
    state_list.push_back(applies ? 0 : -1);
  }
  return state_list;
}

std::vector<int32_t> TimingExceptionMatcher::advanceState(Database& database, const std::vector<int32_t>& state_list,
                                                          std::string_view pin_name, TransType trans_type)
{
  std::vector<int32_t> next_state_list = state_list;
  const std::vector<TimingException>& exceptions = database.get_timing_constraint().get_path_exception_list();
  for (std::size_t exception_index = 0;
       exception_index < exceptions.size() && exception_index < next_state_list.size(); ++exception_index) {
    int32_t& state = next_state_list[exception_index];
    if (state < 0) {
      continue;
    }
    const std::vector<TimingExceptionThrough>& through_list = exceptions[exception_index].get_through_list();
    while (static_cast<std::size_t>(state) < through_list.size()) {
      const TimingExceptionThrough& through = through_list[state];
      if (!matchesThroughObjects(database, through.get_objects(), pin_name)
          || (through.get_trans_type() != TransType::kNone && through.get_trans_type() != trans_type)) {
        break;
      }
      ++state;
    }
  }
  return next_state_list;
}

std::string TimingExceptionMatcher::makePathStateTag(Database& database, std::string_view start, std::string_view clock,
                                                     const std::vector<int32_t>& exception_state_list)
{
  std::string tag(clock);
  if (PWUTIL.getLaunchClockTransition(database, start) == TransType::kFall) {
    tag += "\x1e";
  }
  for (const int32_t state : exception_state_list) {
    tag += "\x1f" + std::to_string(state);
  }
  return tag;
}

bool TimingExceptionMatcher::isFalsePath(Database& database, std::string_view start, std::string_view launch_clock, std::string_view end,
                                         std::string_view capture_clock, AnalysisType analysis_type, TransType end_trans_type,
                                         TransType capture_clock_trans_type, const std::vector<int32_t>& exception_state_list)
{
  return resolve(database, start, launch_clock, end, capture_clock, analysis_type, end_trans_type,
                 capture_clock_trans_type, exception_state_list)
      .false_path;
}

ResolvedTimingExceptions TimingExceptionMatcher::resolve(Database& database, std::string_view start, std::string_view launch_clock,
                                                          std::string_view end, std::string_view capture_clock, AnalysisType analysis_type,
                                                          TransType end_trans_type, TransType capture_clock_trans_type,
                                                          const std::vector<int32_t>& exception_state_list)
{
  ResolvedTimingExceptions resolved;
  for (const TimingClockGroup& relation : database.get_timing_constraint().get_clock_group_list()) {
    if (relation.get_allow_paths() || launch_clock.empty() || capture_clock.empty()) {
      continue;
    }
    int launch_group = -1;
    int capture_group = -1;
    const std::vector<std::set<std::string>>& groups = relation.get_groups();
    for (std::size_t index = 0; index < groups.size(); ++index) {
      if (groups[index].contains(std::string(launch_clock))) {
        launch_group = static_cast<int>(index);
      }
      if (groups[index].contains(std::string(capture_clock))) {
        capture_group = static_cast<int>(index);
      }
    }
    if (launch_group != capture_group && ((launch_group >= 0 && capture_group >= 0) || groups.size() == 1)) {
      resolved.false_path = true;
      return resolved;
    }
  }

  int path_delay_specificity = -1;
  int setup_multicycle_specificity = -1;
  int hold_multicycle_specificity = -1;
  const std::vector<TimingException>& exceptions = database.get_timing_constraint().get_path_exception_list();
  for (std::size_t index = 0; index < exceptions.size() && index < exception_state_list.size(); ++index) {
    const TimingException& exception = exceptions[index];
    const bool transition_matches
        = (end_trans_type == TransType::kRise && exception.get_rise()) || (end_trans_type == TransType::kFall && exception.get_fall());
    if (!isRelevantToAnalysis(exception, analysis_type) || !transition_matches || exception_state_list[index] < 0
        || static_cast<std::size_t>(exception_state_list[index]) != exception.get_through_list().size()
        || !matchesEndpointObjects(database, exception.get_to_objects(), end, capture_clock, false)) {
      continue;
    }
    if (exception.get_to_trans_type() != TransType::kNone) {
      const bool clock_selector = !capture_clock.empty() && exception.get_to_objects().contains(std::string(capture_clock));
      const TransType actual_trans_type = clock_selector ? capture_clock_trans_type : end_trans_type;
      if (actual_trans_type != exception.get_to_trans_type()) {
        continue;
      }
    }

    const int specificity = getSpecificity(database, exception, start, launch_clock, end, capture_clock);
    switch (exception.get_type()) {
      case TimingExceptionType::kFalsePath:
        resolved.false_path = true;
        break;
      case TimingExceptionType::kMaxDelay:
      case TimingExceptionType::kMinDelay:
        if (isTighterPathDelay(exception, resolved.path_delay, specificity, path_delay_specificity)) {
          resolved.path_delay = &exception;
          path_delay_specificity = specificity;
        }
        break;
      case TimingExceptionType::kMulticycle:
        if (exception.get_setup_multiplier().has_value()
            && isTighterMulticycle(resolved.setup_multicycle, *exception.get_setup_multiplier(),
                                   resolved.setup_multicycle == nullptr ? std::nullopt
                                                                       : resolved.setup_multicycle->get_setup_multiplier(),
                                   specificity,
                                   setup_multicycle_specificity)) {
          resolved.setup_multicycle = &exception;
          setup_multicycle_specificity = specificity;
        }
        if (analysis_type == AnalysisType::kMin && exception.get_hold_multiplier().has_value()
            && isTighterMulticycle(resolved.hold_multicycle, *exception.get_hold_multiplier(),
                                   resolved.hold_multicycle == nullptr ? std::nullopt
                                                                      : resolved.hold_multicycle->get_hold_multiplier(),
                                   specificity,
                                   hold_multicycle_specificity)) {
          resolved.hold_multicycle = &exception;
          hold_multicycle_specificity = specificity;
        }
        break;
    }
  }
  return resolved;
}

bool TimingExceptionMatcher::matchesThroughObjects(Database& database, const std::set<std::string>& objects, std::string_view pin_name)
{
  if (objects.contains(std::string(pin_name))) {
    return true;
  }
  const auto pin = database.get_pin_map().find(std::string(pin_name));
  if (pin == database.get_pin_map().end()) {
    return false;
  }
  return (!pin->second.get_instance_name().empty() && objects.contains(pin->second.get_instance_name()))
         || (!pin->second.get_net_name().empty() && objects.contains(pin->second.get_net_name()));
}

bool TimingExceptionMatcher::matchesEndpointObjects(Database& database, const std::set<std::string>& objects, std::string_view pin_name,
                                                    std::string_view clock_name, bool start)
{
  if (objects.empty() || objects.contains(std::string(pin_name)) || objects.contains(std::string(clock_name))) {
    return true;
  }
  const auto pin = database.get_pin_map().find(std::string(pin_name));
  if (pin == database.get_pin_map().end() || pin->second.get_is_port()) {
    return false;
  }
  const auto instance = database.get_instance_map().find(pin->second.get_instance_name());
  if (instance == database.get_instance_map().end()) {
    return false;
  }
  return objects.contains(instance->first) || (start && objects.contains(instance->second.get_clock_pin_name()));
}

}  // namespace ipw
