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
#include "STAHeader.hpp"

namespace ista {

class InstancePower
{
 public:
  InstancePower() = default;
  ~InstancePower() = default;
  // getter
  uint64_t get_instance_id() const { return _instance_id; }
  std::string& get_instance_name() { return _instance_name; }
  PowerGroupType get_power_group_type() const { return _power_group_type; }
  double get_voltage() const { return _voltage; }
  PowerValue& get_power_value() { return _power_value; }
  // setter
  void set_instance_id(const uint64_t instance_id) { _instance_id = instance_id; }
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_power_group_type(const PowerGroupType& power_group_type) { _power_group_type = power_group_type; }
  void set_voltage(const double voltage) { _voltage = voltage; }
  void set_power_value(const PowerValue& power_value) { _power_value = power_value; }
  // function

 private:
  uint64_t _instance_id = 0;
  std::string _instance_name;
  PowerGroupType _power_group_type = PowerGroupType::kNone;
  double _voltage = 0.0;
  PowerValue _power_value;
};

}  // namespace ista
