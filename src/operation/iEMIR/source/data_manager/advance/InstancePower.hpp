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

#include "EMIRHeader.hpp"

namespace iemir {

class InstancePower
{
 public:
  InstancePower() = default;
  ~InstancePower() = default;
  // getter
  uint64_t get_instance_id() { return _instance_id; }
  uint32_t get_power_group_type() { return _power_group_type; }
  double get_voltage() { return _voltage; }
  double get_internal_power() { return _internal_power; }
  double get_switching_power() { return _switching_power; }
  double get_leakage_power() { return _leakage_power; }
  double get_total_power() { return _internal_power + _switching_power + _leakage_power; }
  // setter
  void set_instance_id(uint64_t instance_id) { _instance_id = instance_id; }
  void set_power_group_type(uint32_t power_group_type) { _power_group_type = power_group_type; }
  void set_voltage(double voltage) { _voltage = voltage; }
  void set_internal_power(double internal_power) { _internal_power = internal_power; }
  void set_switching_power(double switching_power) { _switching_power = switching_power; }
  void set_leakage_power(double leakage_power) { _leakage_power = leakage_power; }
  // function

 private:
  uint64_t _instance_id = 0;
  uint32_t _power_group_type = 0;
  double _voltage = 0.0;
  double _internal_power = 0.0;
  double _switching_power = 0.0;
  double _leakage_power = 0.0;
};

}  // namespace iemir
