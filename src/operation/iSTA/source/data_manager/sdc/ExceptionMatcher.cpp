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
#include "sdc/ExceptionMatcher.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "network/Instance.hpp"
#include "network/Network.hpp"
#include "network/Pin.hpp"
#include "sdc/Sdc.hpp"

namespace ista {

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
                                                 : exception.get_setup_multiplier().has_value() || exception.get_hold_multiplier().has_value();
  }
  return false;
}

int endpointSelectorSpecificity(const Network& network, const ExceptionObjects& objects, std::string_view pin_name, std::string_view clock_name, bool start,
                                int concrete_score, int clock_score)
{
  if (objects.empty()) {
    return 0;
  }
  bool concrete
      = (objects.contains({ExceptionObjectType::kPort, std::string(pin_name)}) || objects.contains({ExceptionObjectType::kPin, std::string(pin_name)}));
  const auto pin = network.pins().find(std::string(pin_name));
  if (pin != network.pins().end() && !pin->second.get_is_port()) {
    concrete = concrete || objects.contains({ExceptionObjectType::kCell, pin->second.get_instance_name()});
    const auto instance = network.instances().find(pin->second.get_instance_name());
    concrete
        = concrete || (start && instance != network.instances().end() && objects.contains({ExceptionObjectType::kPin, instance->second.get_clock_pin_name()}));
  }
  if (concrete) {
    return concrete_score;
  }
  return objects.contains({ExceptionObjectType::kClock, std::string(clock_name)}) ? clock_score : 0;
}

int getSpecificity(const Network& network, const TimingException& exception, std::string_view start, std::string_view launch_clock, std::string_view end,
                   std::string_view capture_clock)
{
  // Keep these bits aligned with OpenSTA's endpoint/through priority order.
  int score = endpointSelectorSpecificity(network, exception.get_from_objects(), start, launch_clock, true, 1 << 6, 1 << 3)
              + endpointSelectorSpecificity(network, exception.get_to_objects(), end, capture_clock, false, 1 << 5, 1 << 2);
  if (!exception.get_through_list().empty()) {
    score += 1 << 4;
  }
  if (exception.get_type() == TimingExceptionType::kMulticycle) {
    score += exception.get_setup() && exception.get_hold() ? 1 : 2;
  }
  return score;
}

bool isTighterPathDelay(const TimingException& candidate, const TimingException* current, int candidate_specificity, int current_specificity)
{
  if (current == nullptr || candidate_specificity != current_specificity) {
    return current == nullptr || candidate_specificity > current_specificity;
  }
  return candidate.get_type() == TimingExceptionType::kMaxDelay ? candidate.get_delay() < current->get_delay() : candidate.get_delay() > current->get_delay();
}

bool isTighterMulticycle(const TimingException* current, int32_t candidate_multiplier, std::optional<int32_t> current_multiplier, int candidate_specificity,
                         int current_specificity)
{
  if (current == nullptr || candidate_specificity != current_specificity) {
    return current == nullptr || candidate_specificity > current_specificity;
  }
  return !current_multiplier.has_value() || candidate_multiplier < *current_multiplier;
}

}  // namespace

void ExceptionThroughIndex::rebuild(const Sdc& sdc, const Network& network)
{
  decltype(_groups) groups;
  for (const auto& exception : sdc.get_path_exception_list()) {
    auto& indexed = groups.emplace_back();
    for (const auto& through : exception.get_through_list()) {
      auto& nets = indexed.emplace_back();
      for (const auto& object : through.get_objects()) {
        if (object.type == ExceptionObjectType::kPin && network.isHierarchyPin(object.name))
          nets[network.checkedHierarchyPin(object.name).net].push_back(object.name);
        if (object.type == ExceptionObjectType::kNet && !network.netSegments().empty()) {
          const auto& segment = network.netSegment(object.name);
          for (const auto& pin : segment.pins)
            if (network.isHierarchyPin(pin))
              nets[network.checkedHierarchyPin(pin).net].push_back(pin);
        }
      }
    }
  }
  _groups.swap(groups);
}

const std::vector<std::string>& ExceptionThroughIndex::candidates(std::size_t exception, std::size_t through, const std::string& net) const
{
  static const std::vector<std::string> empty;
  if (exception >= _groups.size() || through >= _groups[exception].size())
    return empty;
  const auto& nets = _groups[exception][through];
  const auto found = nets.find(net);
  return found == nets.end() ? empty : found->second;
}

std::vector<int> ExceptionMatcher::startSpecificity(std::string_view start, std::string_view clock) const
{
  std::vector<int> result;
  result.reserve(_sdc.get_path_exception_list().size());
  for (const auto& exception : _sdc.get_path_exception_list())
    result.push_back(endpointSelectorSpecificity(_network, exception.get_from_objects(), start, clock, true, 1 << 6, 1 << 3));
  return result;
}

std::vector<int32_t> ExceptionMatcher::initState(std::string_view start, std::string_view clock, TransType start_trans_type, AnalysisType analysis_type,
                                                 TransType launch_transition) const
{
  std::vector<int32_t> state_list;
  for (const TimingException& exception : _sdc.get_path_exception_list()) {
    bool applies = isRelevantToAnalysis(exception, analysis_type) && matchesEndpointObjects(exception.get_from_objects(), start, clock, true);
    if (applies && exception.get_from_trans_type() != TransType::kNone) {
      const bool clock_selector = !clock.empty() && exception.get_from_objects().contains({ExceptionObjectType::kClock, std::string(clock)});
      const TransType actual_trans_type = clock_selector ? launch_transition : start_trans_type;
      applies = actual_trans_type == exception.get_from_trans_type();
    }
    state_list.push_back(applies ? 0 : -1);
  }
  return state_list;
}

std::vector<int32_t> ExceptionMatcher::advanceState(const ExceptionThroughIndex& index, const std::vector<int32_t>& state_list, std::string_view pin_name,
                                                    TransType trans_type, std::string_view source_pin) const
{
  std::vector<int32_t> next_state_list = state_list;
  const std::vector<TimingException>& exceptions = _sdc.get_path_exception_list();
  const std::string* net = nullptr;
  bool net_checked = source_pin.empty() || source_pin == pin_name;
  for (std::size_t exception_index = 0; exception_index < exceptions.size() && exception_index < next_state_list.size(); ++exception_index) {
    int32_t& state = next_state_list[exception_index];
    if (state < 0) {
      continue;
    }
    const std::vector<TimingExceptionThrough>& through_list = exceptions[exception_index].get_through_list();
    int previous_crossing = std::numeric_limits<int>::min();
    while (static_cast<std::size_t>(state) < through_list.size()) {
      // Most propagated states have no pending through group. Resolve the
      // hierarchy net only when a group actually needs matching, once per arc.
      if (!net_checked) {
        const auto sink = _network.pins().find(std::string(pin_name));
        if (sink != _network.pins().end())
          net = &sink->second.get_net_name();
        net_checked = true;
      }
      const TimingExceptionThrough& through = through_list[state];
      int crossing = matchesThroughObjects(through.get_objects(), pin_name) ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
      if (net != nullptr) {
        for (const auto& object : index.candidates(exception_index, state, *net)) {
          const auto& boundary = _network.checkedHierarchyPin(object);
          const int position = _network.hierarchyCrossingPosition(boundary, source_pin, pin_name);
          if (position != std::numeric_limits<int>::min() && position >= previous_crossing
              && (crossing == std::numeric_limits<int>::min() || position < crossing))
            crossing = position;
        }
      }
      if (crossing == std::numeric_limits<int>::min() || (through.get_trans_type() != TransType::kNone && through.get_trans_type() != trans_type))
        break;
      previous_crossing = crossing;
      ++state;
    }
  }
  return next_state_list;
}

std::string ExceptionMatcher::makePathStateTag(std::string_view clock, TransType launch_transition, const std::vector<int32_t>& exception_state_list,
                                               bool clock_source_path, std::string_view crpr_path, std::optional<std::size_t> launch_edge_index) const
{
  std::string tag(clock);
  if (launch_edge_index && *launch_edge_index >= 2)
    tag += "\x1b" + std::to_string(*launch_edge_index);
  if (clock_source_path)
    tag += "\x1d";
  if (launch_transition == TransType::kFall) {
    tag += "\x1e";
  }
  for (const int32_t state : exception_state_list) {
    tag += "\x1f" + std::to_string(state);
  }
  if (!crpr_path.empty()) {
    tag += "\x1c" + std::to_string(crpr_path.size()) + ":";
    tag += crpr_path;
  }
  return tag;
}

bool ExceptionMatcher::isFalsePath(std::string_view start, std::string_view launch_clock, std::string_view end, std::string_view capture_clock,
                                   AnalysisType analysis_type, TransType end_trans_type, TransType capture_clock_trans_type,
                                   const std::vector<int32_t>& exception_state_list) const
{
  return resolve(start, launch_clock, end, capture_clock, analysis_type, end_trans_type, capture_clock_trans_type, exception_state_list).false_path;
}

ResolvedTimingExceptions ExceptionMatcher::resolve(std::string_view start, std::string_view launch_clock, std::string_view end, std::string_view capture_clock,
                                                   AnalysisType analysis_type, TransType end_trans_type, TransType capture_clock_trans_type,
                                                   const std::vector<int32_t>& exception_state_list) const
{
  ResolvedTimingExceptions resolved;
  for (const TimingClockGroup& relation : _sdc.get_clock_group_list()) {
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
  const std::vector<TimingException>& exceptions = _sdc.get_path_exception_list();
  for (std::size_t index = 0; index < exceptions.size() && index < exception_state_list.size(); ++index) {
    const TimingException& exception = exceptions[index];
    const bool transition_matches
        = (end_trans_type == TransType::kRise && exception.get_rise()) || (end_trans_type == TransType::kFall && exception.get_fall());
    if (!isRelevantToAnalysis(exception, analysis_type) || !transition_matches || exception_state_list[index] < 0
        || static_cast<std::size_t>(exception_state_list[index]) != exception.get_through_list().size()
        || !matchesEndpointObjects(exception.get_to_objects(), end, capture_clock, false)) {
      continue;
    }
    if (exception.get_to_trans_type() != TransType::kNone) {
      const bool clock_selector = !capture_clock.empty() && exception.get_to_objects().contains({ExceptionObjectType::kClock, std::string(capture_clock)});
      const TransType actual_trans_type = clock_selector ? capture_clock_trans_type : end_trans_type;
      if (actual_trans_type != exception.get_to_trans_type()) {
        continue;
      }
    }

    const int specificity = getSpecificity(_network, exception, start, launch_clock, end, capture_clock);
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
                                   resolved.setup_multicycle == nullptr ? std::nullopt : resolved.setup_multicycle->get_setup_multiplier(), specificity,
                                   setup_multicycle_specificity)) {
          resolved.setup_multicycle = &exception;
          setup_multicycle_specificity = specificity;
        }
        if (analysis_type == AnalysisType::kMin && exception.get_hold_multiplier().has_value()
            && isTighterMulticycle(resolved.hold_multicycle, *exception.get_hold_multiplier(),
                                   resolved.hold_multicycle == nullptr ? std::nullopt : resolved.hold_multicycle->get_hold_multiplier(), specificity,
                                   hold_multicycle_specificity)) {
          resolved.hold_multicycle = &exception;
          hold_multicycle_specificity = specificity;
        }
        break;
    }
  }
  return resolved;
}

bool ExceptionMatcher::matchesThroughObjects(const ExceptionObjects& objects, std::string_view pin_name) const
{
  if ((objects.contains({ExceptionObjectType::kPort, std::string(pin_name)}) || objects.contains({ExceptionObjectType::kPin, std::string(pin_name)}))) {
    return true;
  }
  const auto pin = _network.pins().find(std::string(pin_name));
  if (pin == _network.pins().end()) {
    return false;
  }
  for (const auto& name : _network.pinNets(std::string(pin_name)))
    if (objects.contains({ExceptionObjectType::kNet, name}))
      return true;
  return (!pin->second.get_instance_name().empty() && objects.contains({ExceptionObjectType::kCell, pin->second.get_instance_name()}))
         || (_network.netSegments().empty() && !pin->second.get_net_name().empty()
             && objects.contains({ExceptionObjectType::kNet, pin->second.get_net_name()}));
}

bool ExceptionMatcher::matchesEndpointObjects(const ExceptionObjects& objects, std::string_view pin_name, std::string_view clock_name, bool start) const
{
  if (objects.empty()
      || (objects.contains({ExceptionObjectType::kPort, std::string(pin_name)}) || objects.contains({ExceptionObjectType::kPin, std::string(pin_name)}))
      || objects.contains({ExceptionObjectType::kClock, std::string(clock_name)})) {
    return true;
  }
  const auto pin = _network.pins().find(std::string(pin_name));
  if (pin == _network.pins().end() || pin->second.get_is_port()) {
    return false;
  }
  const auto instance = _network.instances().find(pin->second.get_instance_name());
  if (instance == _network.instances().end()) {
    return false;
  }
  return objects.contains({ExceptionObjectType::kCell, instance->first})
         || (start && objects.contains({ExceptionObjectType::kPin, instance->second.get_clock_pin_name()}));
}

}  // namespace ista
