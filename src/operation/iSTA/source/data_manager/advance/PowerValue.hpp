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

namespace ista {

class PowerValue
{
 public:
  PowerValue() = default;
  ~PowerValue() = default;
  // getter
  double get_internal_power() const { return _internal_power; }
  double get_switching_power() const { return _switching_power; }
  double get_leakage_power() const { return _leakage_power; }
  double get_total_power() const { return _internal_power + _switching_power + _leakage_power; }
  // setter
  void set_internal_power(const double internal_power) { _internal_power = internal_power; }
  void set_switching_power(const double switching_power) { _switching_power = switching_power; }
  void set_leakage_power(const double leakage_power) { _leakage_power = leakage_power; }
  // function
  void add_internal_power(const double internal_power) { _internal_power += internal_power; }
  void add_switching_power(const double switching_power) { _switching_power += switching_power; }
  void add_leakage_power(const double leakage_power) { _leakage_power += leakage_power; }
  void add_power_value(PowerValue& power_value)
  {
    add_internal_power(power_value.get_internal_power());
    add_switching_power(power_value.get_switching_power());
    add_leakage_power(power_value.get_leakage_power());
  }

 private:
  double _internal_power = 0.0;
  double _switching_power = 0.0;
  double _leakage_power = 0.0;
};

}  // namespace ista
