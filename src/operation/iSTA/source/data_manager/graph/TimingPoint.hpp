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
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>

#include "common/AnalysisType.hpp"
#include "common/PathSourceType.hpp"
#include "common/TransType.hpp"
#include "graph/ClockState.hpp"
#include "graph/TimingPathState.hpp"

namespace ista {

class TimingPoint
{
 public:
  TimingPoint() = default;
  ~TimingPoint() = default;
  // getter
  double get_arrival() const { return _arrival; }
  double get_required() const { return _required; }
  double get_setup_check_time() const { return _setup_check_time; }
  double get_slack() const { return _slack; }
  double get_launch_time() const { return _launch_time; }
  int32_t get_level() const { return _level; }
  std::string& get_predecessor() { return _predecessor; }
  // Reference-clock selection survives data propagation. The scalar arrival
  // summary may instead describe a data path launched by another clock.
  const std::string& defaultClock() const { return _default_clock; }
  const std::string& launchClock() const { return _launch_clock; }
  std::size_t get_predecessor_arc_idx() const { return _predecessor_arc_idx; }
  std::map<AnalysisType, std::map<TransType, double>>& get_data_slew_map() { return _data_slew_map; }
  // Path tags retain startpoint, launch clock and ordered exception-match state.
  std::map<AnalysisType, std::map<PathSourceType, std::map<TransType, std::map<std::string, TimingPathState>>>>& get_path_state_map()
  {
    return _path_state_map;
  }
  bool get_is_clock_point() const { return _is_clock_point; }
  bool has_clock_state(std::string_view clock_name) const
  {
    const auto* state = find_clock_state(clock_name);
    return state != nullptr && !state->is_source_path_only;
  }
  ClockState& get_clock_state(std::string_view clock_name) { return _clock_state_map[std::string(clock_name)]; }
  const ClockState* find_clock_state(std::string_view clock_name) const
  {
    const auto state = _clock_state_map.find(std::string(clock_name));
    return state == _clock_state_map.end() ? nullptr : &state->second;
  }
  const std::map<std::string, ClockState>& get_clock_state_map() const { return _clock_state_map; }
  const ClockState* findDefaultClockState() const
  {
    const auto* state = find_clock_state(_default_clock);
    return state == nullptr || state->is_source_path_only ? nullptr : state;
  }
  void clearClockStates()
  {
    _clock_state_map.clear();
    _default_clock.clear();
  }
  const auto& get_path_state_map() const { return _path_state_map; }
  // setter
  void set_arrival(const double arrival) { _arrival = arrival; }
  void set_required(const double required) { _required = required; }
  void set_setup_check_time(const double setup_check_time) { _setup_check_time = setup_check_time; }
  void set_slack(const double slack) { _slack = slack; }
  void set_launch_time(const double launch_time) { _launch_time = launch_time; }
  void set_level(const int32_t level) { _level = level; }
  void set_predecessor(std::string_view predecessor) { _predecessor = predecessor; }
  void setDefaultClock(std::string_view clock) { _default_clock = clock; }
  void setLaunchClock(std::string_view clock) { _launch_clock = clock; }
  void set_predecessor_arc_idx(const std::size_t predecessor_arc_idx) { _predecessor_arc_idx = predecessor_arc_idx; }
  void set_data_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& data_slew_map) { _data_slew_map = data_slew_map; }
  void set_path_state_map(const std::map<AnalysisType, std::map<PathSourceType, std::map<TransType, std::map<std::string, TimingPathState>>>>& path_state_map)
  {
    _path_state_map = path_state_map;
  }
  void set_is_clock_point(const bool is_clock_point) { _is_clock_point = is_clock_point; }
  // function

 private:
  double _arrival = -std::numeric_limits<double>::infinity();
  double _required = std::numeric_limits<double>::infinity();
  double _setup_check_time = 0.0;
  double _slack = 0.0;
  double _launch_time = 0.0;
  int32_t _level = 0;
  std::string _predecessor;
  std::string _default_clock;
  std::string _launch_clock;
  std::size_t _predecessor_arc_idx = std::numeric_limits<std::size_t>::max();
  std::map<AnalysisType, std::map<TransType, double>> _data_slew_map;
  std::map<AnalysisType, std::map<PathSourceType, std::map<TransType, std::map<std::string, TimingPathState>>>> _path_state_map;
  bool _is_clock_point = false;
  std::map<std::string, ClockState> _clock_state_map;
};

}  // namespace ista
