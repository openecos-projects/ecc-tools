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

#include "PWHeader.hpp"
#include "TimingCell.hpp"

namespace ipw {

class TimingLibrary
{
 public:
  TimingLibrary() = default;
  ~TimingLibrary() = default;
  // getter
  std::optional<std::string>& get_leakage_power_unit() { return _leakage_power_unit; }
  double get_nom_voltage() const { return _nom_voltage; }
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
  void set_leakage_power_unit(const std::optional<std::string>& leakage_power_unit) { _leakage_power_unit = leakage_power_unit; }
  void set_nom_voltage(const double nom_voltage) { _nom_voltage = nom_voltage; }
  void set_slew_lower_threshold_pct_rise(const double slew_lower_threshold_pct_rise) { _slew_lower_threshold_pct_rise = slew_lower_threshold_pct_rise; }
  void set_slew_upper_threshold_pct_rise(const double slew_upper_threshold_pct_rise) { _slew_upper_threshold_pct_rise = slew_upper_threshold_pct_rise; }
  void set_slew_lower_threshold_pct_fall(const double slew_lower_threshold_pct_fall) { _slew_lower_threshold_pct_fall = slew_lower_threshold_pct_fall; }
  void set_slew_upper_threshold_pct_fall(const double slew_upper_threshold_pct_fall) { _slew_upper_threshold_pct_fall = slew_upper_threshold_pct_fall; }
  void set_input_threshold_pct_rise(const double input_threshold_pct_rise) { _input_threshold_pct_rise = input_threshold_pct_rise; }
  void set_output_threshold_pct_rise(const double output_threshold_pct_rise) { _output_threshold_pct_rise = output_threshold_pct_rise; }
  void set_input_threshold_pct_fall(const double input_threshold_pct_fall) { _input_threshold_pct_fall = input_threshold_pct_fall; }
  void set_output_threshold_pct_fall(const double output_threshold_pct_fall) { _output_threshold_pct_fall = output_threshold_pct_fall; }
  void set_slew_derate_from_library(const double slew_derate_from_library) { _slew_derate_from_library = slew_derate_from_library; }
  // function

 private:
  std::optional<std::string> _leakage_power_unit;
  double _nom_voltage = 0.0;
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

}  // namespace ipw
