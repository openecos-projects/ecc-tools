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
#include "TimingCheckArc.hpp"

namespace ista {

class Instance
{
 public:
  Instance() = default;
  ~Instance() = default;
  // getter
  uint64_t get_instance_id() const { return _instance_id; }
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_cell_name() { return _cell_name; }
  std::vector<std::string>& get_pin_name_list() { return _pin_name_list; }
  std::vector<TimingCheckArc>& get_check_arc_list() { return _check_arc_list; }
  std::string& get_clock_pin_name() { return _clock_pin_name; }
  std::string& get_output_pin_name() { return _output_pin_name; }
  std::string& get_data_pin_name() { return _data_pin_name; }
  double get_clock_to_q_delay() const { return _clock_to_q_delay; }
  TimingCellArc& get_clock_to_q_arc() { return _clock_to_q_arc; }
  bool get_is_sequential() const { return _is_sequential; }
  bool get_is_clock_gating() const { return _is_clock_gating; }
  bool get_has_clear_arc() const { return _has_clear_arc; }
  bool get_has_preset_arc() const { return _has_preset_arc; }
  bool get_is_io_cell() const { return _is_io_cell; }
  // setter
  void set_instance_id(const uint64_t instance_id) { _instance_id = instance_id; }
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_pin_name_list(const std::vector<std::string>& pin_name_list) { _pin_name_list = pin_name_list; }
  void set_check_arc_list(const std::vector<TimingCheckArc>& check_arc_list) { _check_arc_list = check_arc_list; }
  void set_clock_pin_name(const std::string& clock_pin_name) { _clock_pin_name = clock_pin_name; }
  void set_output_pin_name(const std::string& output_pin_name) { _output_pin_name = output_pin_name; }
  void set_data_pin_name(const std::string& data_pin_name) { _data_pin_name = data_pin_name; }
  void set_clock_to_q_delay(const double clock_to_q_delay) { _clock_to_q_delay = clock_to_q_delay; }
  void set_clock_to_q_arc(const TimingCellArc& clock_to_q_arc) { _clock_to_q_arc = clock_to_q_arc; }
  void set_is_sequential(const bool is_sequential) { _is_sequential = is_sequential; }
  void set_is_clock_gating(const bool is_clock_gating) { _is_clock_gating = is_clock_gating; }
  void set_has_clear_arc(const bool has_clear_arc) { _has_clear_arc = has_clear_arc; }
  void set_has_preset_arc(const bool has_preset_arc) { _has_preset_arc = has_preset_arc; }
  void set_is_io_cell(const bool is_io_cell) { _is_io_cell = is_io_cell; }
  // function

 private:
  uint64_t _instance_id = 0;
  std::string _instance_name;
  std::string _cell_name;
  std::vector<std::string> _pin_name_list;
  std::vector<TimingCheckArc> _check_arc_list;
  std::string _clock_pin_name;
  std::string _output_pin_name;
  std::string _data_pin_name;
  double _clock_to_q_delay = 0.0;
  TimingCellArc _clock_to_q_arc;
  bool _is_sequential = false;
  bool _is_clock_gating = false;
  bool _has_clear_arc = false;
  bool _has_preset_arc = false;
  bool _is_io_cell = false;
};

}  // namespace ista
