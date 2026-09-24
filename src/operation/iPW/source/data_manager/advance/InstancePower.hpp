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

#include "PowerGroupType.hpp"
#include "PowerValue.hpp"
#include "PWHeader.hpp"

namespace ipw {

class InstancePower
{
 public:
  InstancePower() = default;
  ~InstancePower() = default;
  // getter
  PowerGroupType get_power_group_type() const { return _power_group_type; }
  double get_voltage() const { return _voltage; }
  PowerValue& get_power_value() { return _power_value; }
  // setter
  void set_power_group_type(const PowerGroupType& power_group_type) { _power_group_type = power_group_type; }
  void set_voltage(const double voltage) { _voltage = voltage; }
  void set_power_value(const PowerValue& power_value) { _power_value = power_value; }
  // function

 private:
  PowerGroupType _power_group_type = PowerGroupType::kNone;
  double _voltage = 0.0;
  PowerValue _power_value;
};

}  // namespace ipw
