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
#include "TimingCellArc.hpp"
#include "TimingCellPort.hpp"
#include "TimingCheckArc.hpp"
#include "TimingLeakagePower.hpp"
#include "TimingPowerArc.hpp"

namespace ista {

class TimingCell
{
 public:
  TimingCell() = default;
  ~TimingCell() = default;
  // getter
  std::string& get_cell_name() { return _cell_name; }
  std::string& get_library_name() { return _library_name; }
  double get_area() const { return _area; }
  double get_nom_voltage() const { return _nom_voltage; }
  double get_cell_leakage_power() const { return _cell_leakage_power; }
  std::map<std::string, TimingCellPort>& get_port_map() { return _port_map; }
  std::vector<TimingCellArc>& get_cell_arc_list() { return _cell_arc_list; }
  std::vector<TimingPowerArc>& get_power_arc_list() { return _power_arc_list; }
  std::vector<TimingLeakagePower>& get_leakage_power_list() { return _leakage_power_list; }
  std::vector<TimingCheckArc>& get_check_arc_list() { return _check_arc_list; }
  std::vector<TimingCheckArc>& get_sdf_check_arc_list() { return _sdf_check_arc_list; }
  bool get_is_sequential() const { return _is_sequential; }
  bool get_is_clock_gating() const { return _is_clock_gating; }
  bool get_is_macro() const { return _is_macro; }
  bool get_has_clear_arc() const { return _has_clear_arc; }
  bool get_has_preset_arc() const { return _has_preset_arc; }
  double get_slew_lower_threshold_pct_rise() const { return _slew_lower_threshold_pct_rise; }
  double get_slew_upper_threshold_pct_rise() const { return _slew_upper_threshold_pct_rise; }
  double get_slew_lower_threshold_pct_fall() const { return _slew_lower_threshold_pct_fall; }
  double get_slew_upper_threshold_pct_fall() const { return _slew_upper_threshold_pct_fall; }
  double get_input_threshold_pct_rise() const { return _input_threshold_pct_rise; }
  double get_output_threshold_pct_rise() const { return _output_threshold_pct_rise; }
  double get_input_threshold_pct_fall() const { return _input_threshold_pct_fall; }
  double get_output_threshold_pct_fall() const { return _output_threshold_pct_fall; }
  double get_slew_derate_from_library() const { return _slew_derate_from_library; }
  // setter
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_library_name(const std::string& library_name) { _library_name = library_name; }
  void set_area(const double area) { _area = area; }
  void set_nom_voltage(const double nom_voltage) { _nom_voltage = nom_voltage; }
  void set_cell_leakage_power(const double cell_leakage_power) { _cell_leakage_power = cell_leakage_power; }
  void set_port_map(const std::map<std::string, TimingCellPort>& port_map) { _port_map = port_map; }
  void set_cell_arc_list(const std::vector<TimingCellArc>& cell_arc_list) { _cell_arc_list = cell_arc_list; }
  void set_power_arc_list(const std::vector<TimingPowerArc>& power_arc_list) { _power_arc_list = power_arc_list; }
  void set_leakage_power_list(const std::vector<TimingLeakagePower>& leakage_power_list) { _leakage_power_list = leakage_power_list; }
  void set_check_arc_list(const std::vector<TimingCheckArc>& check_arc_list) { _check_arc_list = check_arc_list; }
  void set_sdf_check_arc_list(const std::vector<TimingCheckArc>& sdf_check_arc_list) { _sdf_check_arc_list = sdf_check_arc_list; }
  void set_is_sequential(const bool is_sequential) { _is_sequential = is_sequential; }
  void set_is_clock_gating(const bool is_clock_gating) { _is_clock_gating = is_clock_gating; }
  void set_is_macro(const bool is_macro) { _is_macro = is_macro; }
  void set_has_clear_arc(const bool has_clear_arc) { _has_clear_arc = has_clear_arc; }
  void set_has_preset_arc(const bool has_preset_arc) { _has_preset_arc = has_preset_arc; }
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
  std::string _cell_name;
  std::string _library_name;
  double _area = 0.0;
  double _nom_voltage = 0.0;
  double _cell_leakage_power = 0.0;
  std::map<std::string, TimingCellPort> _port_map;
  std::vector<TimingCellArc> _cell_arc_list;
  std::vector<TimingPowerArc> _power_arc_list;
  std::vector<TimingLeakagePower> _leakage_power_list;
  std::vector<TimingCheckArc> _check_arc_list;
  std::vector<TimingCheckArc> _sdf_check_arc_list;
  bool _is_sequential = false;
  bool _is_clock_gating = false;
  bool _is_macro = false;
  bool _has_clear_arc = false;
  bool _has_preset_arc = false;
  double _slew_lower_threshold_pct_rise = 0.3;
  double _slew_upper_threshold_pct_rise = 0.7;
  double _slew_lower_threshold_pct_fall = 0.3;
  double _slew_upper_threshold_pct_fall = 0.7;
  double _input_threshold_pct_rise = 0.5;
  double _output_threshold_pct_rise = 0.5;
  double _input_threshold_pct_fall = 0.5;
  double _output_threshold_pct_fall = 0.5;
  double _slew_derate_from_library = 1.0;
};

}  // namespace ista
