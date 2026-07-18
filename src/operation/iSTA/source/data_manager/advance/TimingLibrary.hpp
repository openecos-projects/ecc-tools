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

#include "STAHeader.hpp"
#include "TimingCapacitiveUnit.hpp"
#include "TimingCell.hpp"
#include "TimingResistanceUnit.hpp"
#include "TimingTimeUnit.hpp"

namespace ista {

class TimingLibrary
{
 public:
  TimingLibrary() = default;
  ~TimingLibrary() = default;
  // getter
  bool get_has_library_info() const { return _has_library_info; }
  std::optional<std::string>& get_comment() { return _comment; }
  std::optional<bool>& get_simulation() { return _simulation; }
  std::vector<std::string>& get_library_feature_list() { return _library_feature_list; }
  std::optional<std::string>& get_leakage_power_unit() { return _leakage_power_unit; }
  std::optional<std::string>& get_current_unit_name() { return _current_unit_name; }
  std::optional<std::string>& get_voltage_unit_name() { return _voltage_unit_name; }
  TimingCapacitiveUnit get_cap_unit() const { return _cap_unit; }
  TimingResistanceUnit get_resistance_unit() const { return _resistance_unit; }
  TimingTimeUnit get_time_unit() const { return _time_unit; }
  std::optional<double>& get_default_max_transition() { return _default_max_transition; }
  std::optional<double>& get_default_max_fanout() { return _default_max_fanout; }
  std::optional<double>& get_default_fanout_load() { return _default_fanout_load; }
  std::optional<double>& get_nom_process() { return _nom_process; }
  double get_nom_voltage() const { return _nom_voltage; }
  std::optional<double>& get_nom_temperature() { return _nom_temperature; }
  double get_slew_lower_threshold_pct_rise() const { return _slew_lower_threshold_pct_rise; }
  double get_slew_upper_threshold_pct_rise() const { return _slew_upper_threshold_pct_rise; }
  double get_slew_lower_threshold_pct_fall() const { return _slew_lower_threshold_pct_fall; }
  double get_slew_upper_threshold_pct_fall() const { return _slew_upper_threshold_pct_fall; }
  double get_input_threshold_pct_rise() const { return _input_threshold_pct_rise; }
  double get_output_threshold_pct_rise() const { return _output_threshold_pct_rise; }
  double get_input_threshold_pct_fall() const { return _input_threshold_pct_fall; }
  double get_output_threshold_pct_fall() const { return _output_threshold_pct_fall; }
  double get_slew_derate_from_library() const { return _slew_derate_from_library; }
  std::map<std::string, TimingCell>& get_cell_map() { return _cell_map; }
  // setter
  void set_has_library_info(const bool has_library_info) { _has_library_info = has_library_info; }
  void set_comment(const std::optional<std::string>& comment) { _comment = comment; }
  void set_simulation(const std::optional<bool>& simulation) { _simulation = simulation; }
  void set_library_feature_list(const std::vector<std::string>& library_feature_list) { _library_feature_list = library_feature_list; }
  void set_leakage_power_unit(const std::optional<std::string>& leakage_power_unit) { _leakage_power_unit = leakage_power_unit; }
  void set_current_unit_name(const std::optional<std::string>& current_unit_name) { _current_unit_name = current_unit_name; }
  void set_voltage_unit_name(const std::optional<std::string>& voltage_unit_name) { _voltage_unit_name = voltage_unit_name; }
  void set_cap_unit(const TimingCapacitiveUnit& cap_unit) { _cap_unit = cap_unit; }
  void set_resistance_unit(const TimingResistanceUnit& resistance_unit) { _resistance_unit = resistance_unit; }
  void set_time_unit(const TimingTimeUnit& time_unit) { _time_unit = time_unit; }
  void set_default_max_transition(const std::optional<double>& default_max_transition) { _default_max_transition = default_max_transition; }
  void set_default_max_fanout(const std::optional<double>& default_max_fanout) { _default_max_fanout = default_max_fanout; }
  void set_default_fanout_load(const std::optional<double>& default_fanout_load) { _default_fanout_load = default_fanout_load; }
  void set_nom_process(const std::optional<double>& nom_process) { _nom_process = nom_process; }
  void set_nom_voltage(const double nom_voltage) { _nom_voltage = nom_voltage; }
  void set_nom_temperature(const std::optional<double>& nom_temperature) { _nom_temperature = nom_temperature; }
  void set_slew_lower_threshold_pct_rise(const double slew_lower_threshold_pct_rise)
  {
    _slew_lower_threshold_pct_rise = slew_lower_threshold_pct_rise;
  }
  void set_slew_upper_threshold_pct_rise(const double slew_upper_threshold_pct_rise)
  {
    _slew_upper_threshold_pct_rise = slew_upper_threshold_pct_rise;
  }
  void set_slew_lower_threshold_pct_fall(const double slew_lower_threshold_pct_fall)
  {
    _slew_lower_threshold_pct_fall = slew_lower_threshold_pct_fall;
  }
  void set_slew_upper_threshold_pct_fall(const double slew_upper_threshold_pct_fall)
  {
    _slew_upper_threshold_pct_fall = slew_upper_threshold_pct_fall;
  }
  void set_input_threshold_pct_rise(const double input_threshold_pct_rise) { _input_threshold_pct_rise = input_threshold_pct_rise; }
  void set_output_threshold_pct_rise(const double output_threshold_pct_rise) { _output_threshold_pct_rise = output_threshold_pct_rise; }
  void set_input_threshold_pct_fall(const double input_threshold_pct_fall) { _input_threshold_pct_fall = input_threshold_pct_fall; }
  void set_output_threshold_pct_fall(const double output_threshold_pct_fall) { _output_threshold_pct_fall = output_threshold_pct_fall; }
  void set_slew_derate_from_library(const double slew_derate_from_library) { _slew_derate_from_library = slew_derate_from_library; }
  void set_cell_map(const std::map<std::string, TimingCell>& cell_map) { _cell_map = cell_map; }
  // function

 private:
  bool _has_library_info = false;
  std::optional<std::string> _comment;
  std::optional<bool> _simulation;
  std::vector<std::string> _library_feature_list;
  std::optional<std::string> _leakage_power_unit;
  std::optional<std::string> _current_unit_name;
  std::optional<std::string> _voltage_unit_name;
  TimingCapacitiveUnit _cap_unit = TimingCapacitiveUnit::kPF;
  TimingResistanceUnit _resistance_unit = TimingResistanceUnit::kkOHM;
  TimingTimeUnit _time_unit = TimingTimeUnit::kNS;
  std::optional<double> _default_max_transition;
  std::optional<double> _default_max_fanout;
  std::optional<double> _default_fanout_load;
  std::optional<double> _nom_process;
  double _nom_voltage = 0.0;
  std::optional<double> _nom_temperature;
  double _slew_lower_threshold_pct_rise = 0.3;
  double _slew_upper_threshold_pct_rise = 0.7;
  double _slew_lower_threshold_pct_fall = 0.3;
  double _slew_upper_threshold_pct_fall = 0.7;
  double _input_threshold_pct_rise = 0.5;
  double _output_threshold_pct_rise = 0.5;
  double _input_threshold_pct_fall = 0.5;
  double _output_threshold_pct_fall = 0.5;
  double _slew_derate_from_library = 1.0;
  std::map<std::string, TimingCell> _cell_map;
};

}  // namespace ista
