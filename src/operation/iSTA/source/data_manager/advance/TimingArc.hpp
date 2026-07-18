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
#include "TimingArcSense.hpp"
#include "TimingTable.hpp"
#include "TransType.hpp"

namespace ista {

class TimingArc
{
 public:
  TimingArc() = default;
  ~TimingArc() = default;
  // getter
  TimingArcSense get_sense() const { return _sense; }
  int32_t get_arc_idx() const { return _arc_idx; }
  TransType get_trigger_trans_type() const { return _trigger_trans_type; }
  TransType get_check_trans_type() const { return _check_trans_type; }
  std::string& get_library_name() { return _library_name; }
  std::string& get_sdf_cond() { return _sdf_cond; }
  double get_time_unit_scale() const { return _time_unit_scale; }
  double get_cap_unit_scale() const { return _cap_unit_scale; }
  double get_slew_derate() const { return _slew_derate; }
  double get_slew_lower_threshold_pct_rise() const { return _slew_lower_threshold_pct_rise; }
  double get_slew_upper_threshold_pct_rise() const { return _slew_upper_threshold_pct_rise; }
  double get_slew_lower_threshold_pct_fall() const { return _slew_lower_threshold_pct_fall; }
  double get_slew_upper_threshold_pct_fall() const { return _slew_upper_threshold_pct_fall; }
  double get_input_threshold_pct_rise() const { return _input_threshold_pct_rise; }
  double get_output_threshold_pct_rise() const { return _output_threshold_pct_rise; }
  double get_input_threshold_pct_fall() const { return _input_threshold_pct_fall; }
  double get_output_threshold_pct_fall() const { return _output_threshold_pct_fall; }
  std::map<TransType, TimingTable>& get_delay_table_map() { return _delay_table_map; }
  std::map<TransType, TimingTable>& get_slew_table_map() { return _slew_table_map; }
  std::map<TransType, TimingTable>& get_check_table_map() { return _check_table_map; }
  // setter
  void set_sense(const TimingArcSense& sense) { _sense = sense; }
  void set_arc_idx(const int32_t arc_idx) { _arc_idx = arc_idx; }
  void set_trigger_trans_type(const TransType& trigger_trans_type) { _trigger_trans_type = trigger_trans_type; }
  void set_check_trans_type(const TransType& check_trans_type) { _check_trans_type = check_trans_type; }
  void set_library_name(const std::string& library_name) { _library_name = library_name; }
  void set_sdf_cond(const std::string& sdf_cond) { _sdf_cond = sdf_cond; }
  void set_time_unit_scale(const double time_unit_scale) { _time_unit_scale = time_unit_scale; }
  void set_cap_unit_scale(const double cap_unit_scale) { _cap_unit_scale = cap_unit_scale; }
  void set_slew_derate(const double slew_derate) { _slew_derate = slew_derate; }
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
  void set_delay_table_map(const std::map<TransType, TimingTable>& delay_table_map) { _delay_table_map = delay_table_map; }
  void set_slew_table_map(const std::map<TransType, TimingTable>& slew_table_map) { _slew_table_map = slew_table_map; }
  void set_check_table_map(const std::map<TransType, TimingTable>& check_table_map) { _check_table_map = check_table_map; }
  // function

 private:
  TimingArcSense _sense = TimingArcSense::kNone;
  int32_t _arc_idx = -1;
  TransType _trigger_trans_type = TransType::kNone;
  TransType _check_trans_type = TransType::kNone;
  std::string _library_name;
  std::string _sdf_cond;
  double _time_unit_scale = 1.0;
  double _cap_unit_scale = 1.0;
  double _slew_derate = 1.0;
  double _slew_lower_threshold_pct_rise = 0.3;
  double _slew_upper_threshold_pct_rise = 0.7;
  double _slew_lower_threshold_pct_fall = 0.3;
  double _slew_upper_threshold_pct_fall = 0.7;
  double _input_threshold_pct_rise = 0.5;
  double _output_threshold_pct_rise = 0.5;
  double _input_threshold_pct_fall = 0.5;
  double _output_threshold_pct_fall = 0.5;
  std::map<TransType, TimingTable> _delay_table_map;
  std::map<TransType, TimingTable> _slew_table_map;
  std::map<TransType, TimingTable> _check_table_map;
};

}  // namespace ista
