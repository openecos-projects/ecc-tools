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

#include "AnalysisType.hpp"
#include "PathSourceType.hpp"
#include "STAHeader.hpp"
#include "TimingPathState.hpp"
#include "TransType.hpp"

#include <vector>
#include <string>
#include <string_view>

namespace ista {

class TimingPoint
{
 public:
  TimingPoint() = default;
  ~TimingPoint() = default;
  // getter
  double get_arrival() const { return _arrival; }
  double get_required() const { return _required; }
  double get_slack() const { return _slack; }
  double get_launch_time() const { return _launch_time; }
  int32_t get_level() const { return _level; }
  std::string& get_predecessor() { return _predecessor; }
  std::string& get_clock_name() { return _clock_name; }
  [[nodiscard]] std::string_view get_clock_name() const { return _clock_name; }
  std::size_t get_predecessor_arc_idx() const { return _predecessor_arc_idx; }
  std::map<AnalysisType, std::map<TransType, double>>& get_clock_arrival_map() { return _clock_arrival_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_clock_slew_map() { return _clock_slew_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_physical_clock_arrival_map() { return _physical_clock_arrival_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_physical_clock_slew_map() { return _physical_clock_slew_map; }
  std::map<AnalysisType, std::map<TransType, std::string>>& get_clock_predecessor_map() { return _clock_predecessor_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_clock_predecessor_arc_delay_map() { return _clock_predecessor_arc_delay_map; }
  std::map<AnalysisType, std::map<TransType, TransType>>& get_clock_predecessor_trans_type_map() { return _clock_predecessor_trans_type_map; }
  std::map<AnalysisType, std::map<TransType, std::string>>& get_physical_clock_predecessor_map() { return _physical_clock_predecessor_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_physical_clock_predecessor_arc_delay_map()
  {
    return _physical_clock_predecessor_arc_delay_map;
  }
  std::map<AnalysisType, std::map<TransType, TransType>>& get_physical_clock_predecessor_trans_type_map()
  {
    return _physical_clock_predecessor_trans_type_map;
  }
  std::map<AnalysisType, std::map<TransType, double>>& get_data_slew_map() { return _data_slew_map; }
  // The innermost key is the launch clock name, not the individual startpoint.
  std::map<AnalysisType, std::map<PathSourceType, std::map<TransType, std::map<std::string, TimingPathState>>>>& get_path_state_map()
  {
    return _path_state_map;
  }
  bool get_is_clock_point() const { return _is_clock_point; }
  // setter
  void set_arrival(const double arrival) { _arrival = arrival; }
  void set_required(const double required) { _required = required; }
  void set_slack(const double slack) { _slack = slack; }
  void set_launch_time(const double launch_time) { _launch_time = launch_time; }
  void set_level(const int32_t level) { _level = level; }
  void set_predecessor(std::string_view predecessor) { _predecessor = predecessor; }
  void set_clock_name(std::string_view clock_name) { _clock_name = clock_name; }
  void set_predecessor_arc_idx(const std::size_t predecessor_arc_idx) { _predecessor_arc_idx = predecessor_arc_idx; }
  void set_clock_arrival_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_arrival_map) { _clock_arrival_map = clock_arrival_map; }
  void set_clock_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_slew_map) { _clock_slew_map = clock_slew_map; }
  void set_physical_clock_arrival_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_arrival_map)
  {
    _physical_clock_arrival_map = clock_arrival_map;
  }
  void set_physical_clock_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_slew_map)
  {
    _physical_clock_slew_map = clock_slew_map;
  }
  void set_clock_predecessor_map(const std::map<AnalysisType, std::map<TransType, std::string>>& clock_predecessor_map)
  {
    _clock_predecessor_map = clock_predecessor_map;
  }
  void set_clock_predecessor_arc_delay_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_predecessor_arc_delay_map)
  {
    _clock_predecessor_arc_delay_map = clock_predecessor_arc_delay_map;
  }
  void set_clock_predecessor_trans_type_map(const std::map<AnalysisType, std::map<TransType, TransType>>& clock_predecessor_trans_type_map)
  {
    _clock_predecessor_trans_type_map = clock_predecessor_trans_type_map;
  }
  void set_physical_clock_predecessor_map(const std::map<AnalysisType, std::map<TransType, std::string>>& clock_predecessor_map)
  {
    _physical_clock_predecessor_map = clock_predecessor_map;
  }
  void set_physical_clock_predecessor_arc_delay_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_predecessor_arc_delay_map)
  {
    _physical_clock_predecessor_arc_delay_map = clock_predecessor_arc_delay_map;
  }
  void set_physical_clock_predecessor_trans_type_map(
      const std::map<AnalysisType, std::map<TransType, TransType>>& clock_predecessor_trans_type_map)
  {
    _physical_clock_predecessor_trans_type_map = clock_predecessor_trans_type_map;
  }
  void set_data_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& data_slew_map) { _data_slew_map = data_slew_map; }
  void set_path_state_map(
      const std::map<AnalysisType, std::map<PathSourceType, std::map<TransType, std::map<std::string, TimingPathState>>>>& path_state_map)
  {
    _path_state_map = path_state_map;
  }
  void set_is_clock_point(const bool is_clock_point) { _is_clock_point = is_clock_point; }
  // function

 private:
  double _arrival = -std::numeric_limits<double>::infinity();
  double _required = std::numeric_limits<double>::infinity();
  double _slack = 0.0;
  double _launch_time = 0.0;
  int32_t _level = 0;
  std::string _predecessor;
  std::string _clock_name;
  std::size_t _predecessor_arc_idx = std::numeric_limits<std::size_t>::max();
  // Effective state drives timing analysis; physical state retains the full clock-tree calculation.
  std::map<AnalysisType, std::map<TransType, double>> _clock_arrival_map;
  std::map<AnalysisType, std::map<TransType, double>> _clock_slew_map;
  std::map<AnalysisType, std::map<TransType, double>> _physical_clock_arrival_map;
  std::map<AnalysisType, std::map<TransType, double>> _physical_clock_slew_map;
  std::map<AnalysisType, std::map<TransType, std::string>> _clock_predecessor_map;
  std::map<AnalysisType, std::map<TransType, double>> _clock_predecessor_arc_delay_map;
  std::map<AnalysisType, std::map<TransType, TransType>> _clock_predecessor_trans_type_map;
  std::map<AnalysisType, std::map<TransType, std::string>> _physical_clock_predecessor_map;
  std::map<AnalysisType, std::map<TransType, double>> _physical_clock_predecessor_arc_delay_map;
  std::map<AnalysisType, std::map<TransType, TransType>> _physical_clock_predecessor_trans_type_map;
  std::map<AnalysisType, std::map<TransType, double>> _data_slew_map;
  std::map<AnalysisType, std::map<PathSourceType, std::map<TransType, std::map<std::string, TimingPathState>>>> _path_state_map;
  bool _is_clock_point = false;
};

}  // namespace ista
