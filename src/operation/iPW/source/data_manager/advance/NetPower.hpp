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

#include "PowerActivityOrigin.hpp"
#include "PWHeader.hpp"

namespace ipw {

class NetPower
{
 public:
  NetPower() = default;
  ~NetPower() = default;
  // getter
  double get_voltage() const { return _voltage; }
  double get_total_net_load() const { return _total_net_load; }
  double get_static_probability() const { return _static_probability; }
  double get_transition_density() const { return _transition_density; }
  double get_switching_power() const { return _switching_power; }
  PowerActivityOrigin get_activity_origin() const { return _activity_origin; }
  bool get_is_activity_valid() const { return _is_activity_valid; }
  // setter
  void set_voltage(const double voltage) { _voltage = voltage; }
  void set_total_net_load(const double total_net_load) { _total_net_load = total_net_load; }
  void set_static_probability(const double static_probability) { _static_probability = static_probability; }
  void set_transition_density(const double transition_density) { _transition_density = transition_density; }
  void set_switching_power(const double switching_power) { _switching_power = switching_power; }
  void set_activity_origin(const PowerActivityOrigin& activity_origin) { _activity_origin = activity_origin; }
  void set_is_activity_valid(const bool is_activity_valid) { _is_activity_valid = is_activity_valid; }

 private:
  double _voltage = 0.0;
  double _total_net_load = 0.0;
  double _static_probability = 0.0;
  double _transition_density = 0.0;
  double _switching_power = 0.0;
  PowerActivityOrigin _activity_origin = PowerActivityOrigin::kNone;
  bool _is_activity_valid = false;
};

}  // namespace ipw
