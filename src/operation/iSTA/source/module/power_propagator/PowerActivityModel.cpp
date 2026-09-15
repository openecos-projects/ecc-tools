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
#include "PowerActivityModel.hpp"

namespace ista {

PowerActivity PowerActivityModel::getClockActivity(TimingClock& timing_clock) const
{
  if (timing_clock.get_period() <= STA_ERROR) {
    return PowerActivity();
  }

  double duty = (timing_clock.get_fall_edge() - timing_clock.get_rise_edge()) / timing_clock.get_period();
  if (duty <= STA_ERROR || duty >= 1.0 - STA_ERROR) {
    duty = 0.5;
  }

  PowerActivity activity;
  activity.set_transition_density(2.0 / timing_clock.get_period());
  activity.set_static_probability(duty);
  activity.set_origin(PowerActivityOrigin::kClock);
  activity.set_is_valid(true);
  return activity;
}

PowerActivity PowerActivityModel::getDefaultInputActivity(double minimum_clock_period) const
{
  PowerActivity activity;
  activity.set_transition_density(getDefaultTransitionDensity(minimum_clock_period));
  activity.set_static_probability(_default_static_probability);
  activity.set_origin(PowerActivityOrigin::kInput);
  activity.set_is_valid(minimum_clock_period > STA_ERROR);
  return activity;
}

PowerActivity PowerActivityModel::getInitialSequentialOutputActivity() const
{
  PowerActivity activity;
  activity.set_static_probability(_default_static_probability);
  activity.set_origin(PowerActivityOrigin::kSequential);
  activity.set_is_valid(true);
  return activity;
}

PowerActivity PowerActivityModel::getClockGateOutputActivity(const PowerActivity& clock_activity,
                                                             const PowerActivity& enable_activity) const
{
  if (!clock_activity.get_is_valid() || !enable_activity.get_is_valid()) {
    return PowerActivity();
  }

  double clock_probability = clock_activity.get_static_probability();
  double enable_probability = enable_activity.get_static_probability();

  PowerActivity activity;
  activity.set_rise_transition_density(clock_activity.get_rise_transition_density() * enable_probability
                                       + enable_activity.get_rise_transition_density() * clock_probability);
  activity.set_fall_transition_density(clock_activity.get_fall_transition_density() * enable_probability
                                       + enable_activity.get_fall_transition_density() * clock_probability);
  activity.set_static_probability(clock_probability * enable_probability);
  activity.set_origin(PowerActivityOrigin::kPropagated);
  activity.set_is_valid(true);
  return activity;
}

void PowerActivityModel::limitDataActivity(Database& database, std::string& pin_name, PowerActivity& activity,
                                           double minimum_clock_period) const
{
  if (!shouldLimitDataActivity(database, pin_name, activity)) {
    return;
  }
  if (minimum_clock_period <= STA_ERROR) {
    return;
  }

  double default_transition_density = getDefaultTransitionDensity(minimum_clock_period);
  double probability_limited_transition_density = getProbabilityLimitedTransitionDensity(activity, minimum_clock_period);
  scaleTransitionDensity(activity, std::min(default_transition_density, probability_limited_transition_density));
}

void PowerActivityModel::limitSequentialOutputActivity(PowerActivity& output_activity, const PowerActivity& data_activity,
                                                       const PowerActivity& clock_activity) const
{
  if (!clock_activity.get_is_valid()) {
    return;
  }

  double active_clock_transition_density = clock_activity.get_transition_density() / 2.0;
  if (data_activity.get_transition_density() <= active_clock_transition_density) {
    return;
  }

  double probability = data_activity.get_static_probability();
  output_activity.set_transition_density(2.0 * probability * (1.0 - probability) * active_clock_transition_density);
}

double PowerActivityModel::getDefaultTransitionDensity(double minimum_clock_period) const
{
  return minimum_clock_period > STA_ERROR ? _default_toggle_rate / minimum_clock_period : 0.0;
}

double PowerActivityModel::getProbabilityLimitedTransitionDensity(const PowerActivity& activity, double minimum_clock_period) const
{
  if (minimum_clock_period <= STA_ERROR) {
    return 0.0;
  }

  double probability = activity.get_static_probability();
  return 2.0 * probability * (1.0 - probability) / minimum_clock_period;
}

bool PowerActivityModel::shouldLimitDataActivity(Database& database, std::string& pin_name, PowerActivity& activity) const
{
  if (!activity.get_is_valid() || activity.get_origin() == PowerActivityOrigin::kClock
      || activity.get_origin() == PowerActivityOrigin::kVcd || activity.get_origin() == PowerActivityOrigin::kConstant) {
    return false;
  }
  if (database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point()) {
    return false;
  }
  return true;
}

void PowerActivityModel::scaleTransitionDensity(PowerActivity& activity, double maximum_transition_density) const
{
  double transition_density = activity.get_transition_density();
  if (maximum_transition_density <= STA_ERROR) {
    activity.set_transition_density(0.0);
    return;
  }
  if (transition_density <= maximum_transition_density) {
    return;
  }

  double density_scale = maximum_transition_density / transition_density;
  activity.set_rise_transition_density(activity.get_rise_transition_density() * density_scale);
  activity.set_fall_transition_density(activity.get_fall_transition_density() * density_scale);
}

}  // namespace ista
