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

#include "PowerValue.hpp"
#include "PWHeader.hpp"

namespace ipw {

class PeakPowerValue
{
 public:
  PeakPowerValue() = default;
  ~PeakPowerValue() = default;
  // getter
  double get_power() const { return _power_value.get_total_power(); }
  PowerValue& get_power_value() { return _power_value; }
  double get_begin_time() const { return _begin_time; }
  double get_end_time() const { return _end_time; }
  std::size_t get_window_index() const { return _window_index; }
  // setter
  // function
  void update(PowerValue& power_value, const double begin_time, const double end_time, const std::size_t window_index)
  {
    if (power_value.get_total_power() <= _power_value.get_total_power()) {
      return;
    }
    _power_value = power_value;
    _begin_time = begin_time;
    _end_time = end_time;
    _window_index = window_index;
  }

  // Window results are merged in any order, so an equal power keeps the earliest window.
  void merge(const PeakPowerValue& other)
  {
    if (other._power_value.get_total_power() < _power_value.get_total_power()) {
      return;
    }
    if (other._power_value.get_total_power() == _power_value.get_total_power()
        && other._window_index >= _window_index) {
      return;
    }
    *this = other;
  }

 private:
  PowerValue _power_value;
  double _begin_time = 0.0;
  double _end_time = 0.0;
  std::size_t _window_index = 0;
};

}  // namespace ipw
