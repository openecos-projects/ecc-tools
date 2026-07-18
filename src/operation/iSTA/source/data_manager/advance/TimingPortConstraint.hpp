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

namespace ista {

class TimingPortConstraint
{
 public:
  TimingPortConstraint() = default;
  ~TimingPortConstraint() = default;
  // getter
  std::string& get_port_name() { return _port_name; }
  std::string& get_clock_name() { return _clock_name; }
  double get_input_delay_max() const { return _input_delay_max; }
  double get_input_delay_min() const { return _input_delay_min; }
  double get_output_delay_max() const { return _output_delay_max; }
  double get_output_delay_min() const { return _output_delay_min; }
  double get_input_transition() const { return _input_transition; }
  double get_load() const { return _load; }
  bool get_has_input_delay_max() const { return _has_input_delay_max; }
  bool get_has_input_delay_min() const { return _has_input_delay_min; }
  bool get_has_output_delay_max() const { return _has_output_delay_max; }
  bool get_has_output_delay_min() const { return _has_output_delay_min; }
  bool get_has_input_transition() const { return _has_input_transition; }
  bool get_has_load() const { return _has_load; }
  // setter
  void set_port_name(const std::string& port_name) { _port_name = port_name; }
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_input_delay_max(const double input_delay_max) { _input_delay_max = input_delay_max; }
  void set_input_delay_min(const double input_delay_min) { _input_delay_min = input_delay_min; }
  void set_output_delay_max(const double output_delay_max) { _output_delay_max = output_delay_max; }
  void set_output_delay_min(const double output_delay_min) { _output_delay_min = output_delay_min; }
  void set_input_transition(const double input_transition) { _input_transition = input_transition; }
  void set_load(const double load) { _load = load; }
  void set_has_input_delay_max(const bool has_input_delay_max) { _has_input_delay_max = has_input_delay_max; }
  void set_has_input_delay_min(const bool has_input_delay_min) { _has_input_delay_min = has_input_delay_min; }
  void set_has_output_delay_max(const bool has_output_delay_max) { _has_output_delay_max = has_output_delay_max; }
  void set_has_output_delay_min(const bool has_output_delay_min) { _has_output_delay_min = has_output_delay_min; }
  void set_has_input_transition(const bool has_input_transition) { _has_input_transition = has_input_transition; }
  void set_has_load(const bool has_load) { _has_load = has_load; }
  // function

 private:
  std::string _port_name;
  std::string _clock_name;
  double _input_delay_max = 0.0;
  double _input_delay_min = 0.0;
  double _output_delay_max = 0.0;
  double _output_delay_min = 0.0;
  double _input_transition = 0.0;
  double _load = 0.0;
  bool _has_input_delay_max = false;
  bool _has_input_delay_min = false;
  bool _has_output_delay_max = false;
  bool _has_output_delay_min = false;
  bool _has_input_transition = false;
  bool _has_load = false;
};

}  // namespace ista
