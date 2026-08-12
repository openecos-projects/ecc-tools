// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL-2.0
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// ***************************************************************************************
#pragma once

#include "STAHeader.hpp"

namespace ista {

class PALeakageSummary
{
 public:
  PALeakageSummary() = default;
  ~PALeakageSummary() = default;
  // getter
  bool get_has_conditional() const { return _has_conditional; }
  bool get_has_unconditional() const { return _has_unconditional; }
  double get_conditional_leakage_power() const { return _conditional_leakage_power; }
  double get_conditional_probability() const { return _conditional_probability; }
  double get_unconditional_leakage_power() const { return _unconditional_leakage_power; }
  // setter
  void set_has_conditional(const bool has_conditional) { _has_conditional = has_conditional; }
  void set_has_unconditional(const bool has_unconditional) { _has_unconditional = has_unconditional; }
  void set_conditional_leakage_power(const double conditional_leakage_power) { _conditional_leakage_power = conditional_leakage_power; }
  void set_conditional_probability(const double conditional_probability) { _conditional_probability = conditional_probability; }
  void set_unconditional_leakage_power(const double unconditional_leakage_power)
  {
    _unconditional_leakage_power = unconditional_leakage_power;
  }
  // function
  void add_conditional_leakage_power(const double leakage_power, const double probability)
  {
    _conditional_leakage_power += leakage_power * probability;
    _conditional_probability += probability;
    _has_conditional = true;
  }
  void add_unconditional_leakage_power(const double leakage_power)
  {
    _unconditional_leakage_power += leakage_power;
    _has_unconditional = true;
  }
  double get_leakage_power(const double cell_leakage_power) const
  {
    if (_has_conditional) {
      double remaining_probability = std::max(0.0, 1.0 - _conditional_probability);
      return _conditional_leakage_power + cell_leakage_power * remaining_probability;
    }
    if (_has_unconditional) {
      return _unconditional_leakage_power;
    }
    return cell_leakage_power;
  }

 private:
  bool _has_conditional = false;
  bool _has_unconditional = false;
  double _conditional_leakage_power = 0.0;
  double _conditional_probability = 0.0;
  double _unconditional_leakage_power = 0.0;
};

}  // namespace ista
