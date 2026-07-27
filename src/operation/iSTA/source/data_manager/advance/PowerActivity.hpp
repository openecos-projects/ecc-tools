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
#include "STAHeader.hpp"

namespace ista {

class PowerActivity
{
 public:
  PowerActivity() = default;
  ~PowerActivity() = default;
  // getter
  double get_rise_transition_density() const { return _rise_transition_density; }
  double get_fall_transition_density() const { return _fall_transition_density; }
  double get_transition_density() const { return _rise_transition_density + _fall_transition_density; }
  double get_static_probability() const { return _static_probability; }
  PowerActivityOrigin get_origin() const { return _origin; }
  bool get_is_valid() const { return _is_valid; }
  // setter
  void set_rise_transition_density(const double rise_transition_density) { _rise_transition_density = rise_transition_density; }
  void set_fall_transition_density(const double fall_transition_density) { _fall_transition_density = fall_transition_density; }
  void set_static_probability(const double static_probability) { _static_probability = static_probability; }
  void set_origin(const PowerActivityOrigin& origin) { _origin = origin; }
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  // function
  void set_transition_density(const double transition_density)
  {
    _rise_transition_density = transition_density / 2.0;
    _fall_transition_density = transition_density / 2.0;
  }

 private:
  double _rise_transition_density = 0.0;
  double _fall_transition_density = 0.0;
  double _static_probability = 0.5;
  PowerActivityOrigin _origin = PowerActivityOrigin::kNone;
  bool _is_valid = false;
};

}  // namespace ista
