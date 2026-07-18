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
#include "TimingTableVariableType.hpp"

namespace ista {

class TimingTable
{
 public:
  TimingTable() = default;
  ~TimingTable() = default;
  // getter
  TimingTableVariableType get_variable_type1() const { return _variable_type1; }
  TimingTableVariableType get_variable_type2() const { return _variable_type2; }
  std::vector<std::vector<double>>& get_axis_list() { return _axis_list; }
  std::vector<double>& get_value_list() { return _value_list; }
  // setter
  void set_variable_type1(const TimingTableVariableType& variable_type1) { _variable_type1 = variable_type1; }
  void set_variable_type2(const TimingTableVariableType& variable_type2) { _variable_type2 = variable_type2; }
  void set_axis_list(const std::vector<std::vector<double>>& axis_list) { _axis_list = axis_list; }
  void set_value_list(const std::vector<double>& value_list) { _value_list = value_list; }
  // function
  double findValue(double slew, double constrain_slew_or_load)
  {
    if (_value_list.empty()) {
      return 0.0;
    }
    if (_axis_list.empty()) {
      return _value_list.front();
    }

    double val1 = slew;
    double val2 = constrain_slew_or_load;
    if (_variable_type1 == TimingTableVariableType::kOutputCapacitance || _variable_type1 == TimingTableVariableType::kConstrainedTransition) {
      val1 = constrain_slew_or_load;
      val2 = slew;
    }

    std::size_t num_val1 = getAxisSize(0);
    if (_axis_list.size() == 1) {
      std::tuple<double, double, std::size_t> region1 = getAxisRegion(0, num_val1, val1);
      double x1 = std::get<0>(region1);
      double x2 = std::get<1>(region1);
      std::size_t val1_idx = std::get<2>(region1);
      return linearInterpolate(x1, x2, getTableValue(val1_idx), getTableValue(val1_idx + 1), val1);
    }

    std::size_t num_val2 = getAxisSize(1);
    std::tuple<double, double, std::size_t> region1 = getAxisRegion(0, num_val1, val1);
    std::tuple<double, double, std::size_t> region2 = getAxisRegion(1, num_val2, val2);
    double x1 = std::get<0>(region1);
    double x2 = std::get<1>(region1);
    std::size_t val1_idx = std::get<2>(region1);
    double y1 = std::get<0>(region2);
    double y2 = std::get<1>(region2);
    std::size_t val2_idx = std::get<2>(region2);

    std::size_t index = num_val2 * val1_idx + val2_idx;
    double q11 = getTableValue(index);
    index = num_val2 * (val1_idx + 1) + val2_idx;
    double q21 = getTableValue(index);
    index = num_val2 * val1_idx + (val2_idx + 1);
    double q12 = getTableValue(index);
    index = num_val2 * (val1_idx + 1) + (val2_idx + 1);
    double q22 = getTableValue(index);
    return bilinearInterpolation(q11, q12, q21, q22, x1, x2, y1, y2, val1, val2);
  }

 private:
  std::size_t getAxisSize(std::size_t axis_idx)
  {
    if (axis_idx >= _axis_list.size()) {
      return 0;
    }
    return _axis_list[axis_idx].size();
  }

  std::tuple<double, double, std::size_t> getAxisRegion(std::size_t axis_idx, std::size_t num_val, double val)
  {
    if (num_val < 2) {
      return std::make_tuple(0.0, 0.0, 0);
    }
    double x2 = 0.0;
    std::size_t val_idx = 0;
    for (; val_idx < num_val; val_idx++) {
      x2 = _axis_list[axis_idx][val_idx];
      if (x2 > val) {
        break;
      }
    }

    if (val_idx == num_val) {
      val_idx = num_val - 2;
    } else if (val_idx > 0) {
      val_idx--;
    } else {
      x2 = _axis_list[axis_idx][1];
    }
    double x1 = _axis_list[axis_idx][val_idx];
    return std::make_tuple(x1, x2, val_idx);
  }

  double getTableValue(std::size_t value_idx)
  {
    if (value_idx >= _value_list.size()) {
      return 0.0;
    }
    return _value_list[value_idx];
  }

  double linearInterpolate(double x1, double x2, double y1, double y2, double x)
  {
    if (std::abs(x2 - x1) < STA_ERROR) {
      return y1;
    }
    return y1 + ((x - x1) * (y2 - y1) / (x2 - x1));
  }

  double bilinearInterpolation(double q11, double q12, double q21, double q22, double x1, double x2, double y1, double y2, double x, double y)
  {
    if (std::abs(x2 - x1) < STA_ERROR) {
      return linearInterpolate(y1, y2, q11, q12, y);
    }
    if (std::abs(y2 - y1) < STA_ERROR) {
      return linearInterpolate(x1, x2, q11, q21, x);
    }
    double x2_x1 = x2 - x1;
    double y2_y1 = y2 - y1;
    double x2_x = x2 - x;
    double y2_y = y2 - y;
    double x_x1 = x - x1;
    double y_y1 = y - y1;
    return (q11 * x2_x * y2_y + q21 * x_x1 * y2_y + q12 * x2_x * y_y1 + q22 * x_x1 * y_y1) / (x2_x1 * y2_y1);
  }

  TimingTableVariableType _variable_type1 = TimingTableVariableType::kNone;
  TimingTableVariableType _variable_type2 = TimingTableVariableType::kNone;
  std::vector<std::vector<double>> _axis_list;
  std::vector<double> _value_list;
};

}  // namespace ista
