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

// Keep candidates with different false-path applicability in separate arrival tags.
// A false worst path must not hide a slower/faster valid path from the same clock.
std::string Utility::getPathStateTag(Database& database, std::string_view start, std::string_view clock)
{
  std::string tag(clock);
  if (getLaunchClockTransition(database, start) == TransType::kFall) {
    tag += "\x1e";
  }
  std::size_t index = 0;
  for (const TimingException& exception : database.get_timing_constraint().get_false_path_list()) {
    if (!exception.get_from_objects().empty() && matchesTimingObjects(database, exception.get_from_objects(), start, clock, true)) {
      tag += "\x1f" + std::to_string(index);
    }
    ++index;
  }
  return tag;
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

bool Utility::isFalsePath(Database& database, std::string_view start, std::string_view launch_clock, std::string_view end, std::string_view capture_clock,
                          AnalysisType type)
{
  for (const TimingClockGroup& relation : database.get_timing_constraint().get_clock_group_list()) {
    if (relation.get_allow_paths() || launch_clock.empty() || capture_clock.empty()) {
      continue;
    }
    int launch_group = -1;
    int capture_group = -1;
    const std::vector<std::set<std::string>>& groups = relation.get_groups();
    for (std::size_t i = 0; i < groups.size(); ++i) {
      if (groups[i].contains(std::string(launch_clock))) {
        launch_group = static_cast<int>(i);
      }
      if (groups[i].contains(std::string(capture_clock))) {
        capture_group = static_cast<int>(i);
      }
    }
    if (launch_group != capture_group && ((launch_group >= 0 && capture_group >= 0) || groups.size() == 1)) {
      return true;
    }
  }
  for (const TimingException& exception : database.get_timing_constraint().get_false_path_list()) {
    if ((type == AnalysisType::kMax && exception.get_setup()) || (type == AnalysisType::kMin && exception.get_hold())) {
      if (matchesTimingObjects(database, exception.get_from_objects(), start, launch_clock, true)
          && matchesTimingObjects(database, exception.get_to_objects(), end, capture_clock, false)) {
        return true;
      }
    }
  }
  return false;
}

bool Utility::matchesTimingObjects(Database& database, const std::set<std::string>& objects, std::string_view pin_name, std::string_view clock_name, bool start)
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

// private

Utility* Utility::_util_instance = nullptr;

}  // namespace ista
