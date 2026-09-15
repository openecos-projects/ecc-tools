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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "PowerActivity.hpp"

namespace ista {

class PowerActivityModel
{
 public:
  PowerActivityModel() = default;
  ~PowerActivityModel() = default;

  PowerActivity getClockActivity(TimingClock& timing_clock) const;
  PowerActivity getDefaultInputActivity(double minimum_clock_period) const;
  PowerActivity getInitialSequentialOutputActivity() const;
  PowerActivity getClockGateOutputActivity(const PowerActivity& clock_activity, const PowerActivity& enable_activity) const;
  void limitDataActivity(Database& database, std::string& pin_name, PowerActivity& activity, double minimum_clock_period) const;
  void limitSequentialOutputActivity(PowerActivity& output_activity, const PowerActivity& data_activity,
                                     const PowerActivity& clock_activity) const;
  double getDefaultTransitionDensity(double minimum_clock_period) const;

 private:
  double getProbabilityLimitedTransitionDensity(const PowerActivity& activity, double minimum_clock_period) const;
  bool shouldLimitDataActivity(Database& database, std::string& pin_name, PowerActivity& activity) const;
  void scaleTransitionDensity(PowerActivity& activity, double maximum_transition_density) const;

  double _default_toggle_rate = 0.1;
  double _default_static_probability = 0.5;
};

}  // namespace ista
