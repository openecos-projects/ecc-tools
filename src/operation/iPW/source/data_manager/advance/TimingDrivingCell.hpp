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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PWHeader.hpp"

namespace ipw {

class TimingDrivingCell
{
 public:
  TimingDrivingCell() = default;
  ~TimingDrivingCell() = default;
  // getter
  std::string& get_library_name() { return _library_name; }
  std::string& get_cell_name() { return _cell_name; }
  std::string& get_from_pin() { return _from_pin; }
  std::string& get_to_pin() { return _to_pin; }
  double get_input_transition_rise() const { return _input_transition_rise; }
  double get_input_transition_fall() const { return _input_transition_fall; }
  // setter
  void set_library_name(const std::string& library_name) { _library_name = library_name; }
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_from_pin(const std::string& from_pin) { _from_pin = from_pin; }
  void set_to_pin(const std::string& to_pin) { _to_pin = to_pin; }
  void set_input_transition_rise(const double input_transition_rise) { _input_transition_rise = input_transition_rise; }
  void set_input_transition_fall(const double input_transition_fall) { _input_transition_fall = input_transition_fall; }
  // function

 private:
  std::string _library_name;
  std::string _cell_name;
  std::string _from_pin;
  std::string _to_pin;
  double _input_transition_rise = 0.0;
  double _input_transition_fall = 0.0;
};

}  // namespace ipw
