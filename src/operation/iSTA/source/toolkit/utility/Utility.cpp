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
#include "Utility.hpp"

#include "Database.hpp"

namespace ista {

// public

void Utility::initInst()
{
  if (_util_instance == nullptr) {
    _util_instance = new Utility();
  }
}

Utility& Utility::getInst()
{
  if (_util_instance == nullptr) {
    initInst();
  }
  return *_util_instance;
}

void Utility::destroyInst()
{
  if (_util_instance != nullptr) {
    delete _util_instance;
    _util_instance = nullptr;
  }
}

TransType Utility::getLaunchClockTransition(Database& database, std::string_view start)
{
  const auto pin = database.get_pin_map().find(std::string(start));
  if (pin != database.get_pin_map().end() && !pin->second.get_is_port()) {
    const auto instance = database.get_instance_map().find(pin->second.get_instance_name());
    if (instance != database.get_instance_map().end() && instance->second.get_is_sequential()) {
      std::vector<TimingArc>& arcs = instance->second.get_clock_to_q_arc().get_timing_arc_list();
      if (!arcs.empty() && arcs.front().get_trigger_trans_type() == TransType::kFall) {
        return TransType::kFall;
      }
    }
  }
  return TransType::kRise;
}

double Utility::getLaunchClockEdge(Database& database, std::string_view start, std::string_view clock)
{
  const auto definition = database.get_timing_constraint().get_clock_map().find(std::string(clock));
  if (definition == database.get_timing_constraint().get_clock_map().end()) {
    return 0.0;
  }
  return getLaunchClockTransition(database, start) == TransType::kFall ? definition->second.get_fall_edge() : definition->second.get_rise_edge();
}

double Utility::getClockEdgeSeparation(double launch_period, double capture_period, double launch_edge, double capture_edge, AnalysisType type)
{
  // All relative edge positions repeat modulo the greatest common period.
  // Use a floating Euclidean algorithm to retain rational divide/multiply ratios.
  double interval = std::max(launch_period, capture_period);
  double remainder = std::min(launch_period, capture_period);
  const double tolerance = interval * 1e-10;
  if (!std::isfinite(interval) || remainder <= 0) {
    throw std::invalid_argument("invalid clock period");
  }
  for (int iteration = 0; remainder > tolerance && iteration < 128; ++iteration) {
    const double next = std::fmod(interval, remainder);
    interval = remainder;
    remainder = std::min(next, remainder - next);
  }
  double separation = std::fmod(capture_edge - launch_edge, interval);
  if (separation < 0) {
    separation += interval;
  }
  if (separation < tolerance || interval - separation < tolerance) {
    separation = 0;
  }
  if (type == AnalysisType::kMax) {
    return separation == 0 ? interval : separation;
  }
  return separation == 0 ? 0 : separation - interval;
}

TransType Utility::getClockSourceTransition(Database& database, std::string_view clock_pin, std::string_view clock_name, AnalysisType analysis_type,
                                            TransType endpoint_trans_type)
{
  const auto clock = database.get_timing_constraint().get_clock_map().find(std::string(clock_name));
  if (clock == database.get_timing_constraint().get_clock_map().end())
    return endpoint_trans_type;
  std::set<std::pair<std::string, TransType>> visited;
  std::string current_pin(clock_pin);
  TransType current_transition = endpoint_trans_type;
  for (std::size_t depth = 0; depth <= database.get_timing_point_map().size(); ++depth) {
    if (std::find(clock->second.get_source_list().begin(), clock->second.get_source_list().end(), current_pin) != clock->second.get_source_list().end())
      return current_transition;
    if (!visited.emplace(current_pin, current_transition).second)
      return current_transition;
    const auto point = database.get_timing_point_map().find(current_pin);
    if (point == database.get_timing_point_map().end())
      return current_transition;
    const TimingClockPointState* state = point->second.find_clock_state(clock_name);
    if (state == nullptr)
      return current_transition;
    const auto find_predecessor = [&](const auto& transition_map, const auto& pin_map) -> std::optional<std::pair<std::string, TransType>> {
      const auto analysis_iter = transition_map.find(analysis_type);
      if (analysis_iter == transition_map.end())
        return std::nullopt;
      const auto transition_iter = analysis_iter->second.find(current_transition);
      if (transition_iter == analysis_iter->second.end() || transition_iter->second == TransType::kNone)
        return std::nullopt;
      const auto pin_analysis_iter = pin_map.find(analysis_type);
      if (pin_analysis_iter == pin_map.end())
        return std::nullopt;
      const auto pin_iter = pin_analysis_iter->second.find(current_transition);
      if (pin_iter == pin_analysis_iter->second.end() || pin_iter->second.empty())
        return std::nullopt;
      return std::make_pair(pin_iter->second, transition_iter->second);
    };
    auto predecessor = find_predecessor(state->predecessor_trans_type_map, state->predecessor_map);
    if (!predecessor.has_value())
      predecessor = find_predecessor(state->physical_predecessor_trans_type_map, state->physical_predecessor_map);
    if (!predecessor.has_value())
      return current_transition;
    current_pin = predecessor->first;
    current_transition = predecessor->second;
  }
  return current_transition;
}

// private

Utility* Utility::_util_instance = nullptr;

}  // namespace ista
