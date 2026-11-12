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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DmpDelayCalculator.hpp"

#include <cmath>
#include <limits>

#include "DmpCeffCalculator.hpp"
#include "ThresholdNormalizer.hpp"
#include "TimingTolerance.hpp"

namespace ista {

DriverResponse DmpDelayCalculator::calculate(TimingArc& timing_arc, TransType output_trans_type, double input_slew, const ParasiticDmpModel& dmp_model,
                                             double output_load) const
{
  DriverResponse result;
  if (!dmp_model.get_is_valid()) {
    return result;
  }
  double gate_delay = 0.0;
  double driver_slew = 0.0;
  double effective_capacitance = output_load;
  if (!DmpCeffCalculator{}.calculate(timing_arc, output_trans_type, input_slew, dmp_model, gate_delay, driver_slew, effective_capacitance)) {
    return result;
  }
  result.set_is_valid(true);
  result.set_effective_capacitance(effective_capacitance);
  result.set_gate_delay(gate_delay);
  result.set_driver_slew(driver_slew);
  for (const auto& [node, load_model] : dmp_model.get_load_model_map()) {
    double wire_delay = 0.0;
    double load_slew = driver_slew;
    if (!calculateLoad(load_model, timing_arc, output_trans_type, driver_slew, wire_delay, load_slew)) {
      continue;
    }
    result.get_wire_delay_map()[node] = wire_delay;
    result.get_load_slew_map()[node] = load_slew;
  }
  return result;
}

bool DmpDelayCalculator::calculateLoad(const ParasiticDmpLoadModel& load_model, TimingArc& timing_arc, TransType trans_type, double driver_slew,
                                       double& wire_delay, double& load_slew)
{
  if (!load_model.get_is_valid() || load_model.get_pole_list().empty() || load_model.get_residue_list().empty()) {
    return false;
  }
  double pole1 = load_model.get_pole_list().front();
  if (!(pole1 > 0.0)) {
    return false;
  }
  wire_delay = 1.0 / pole1;
  load_slew = driver_slew;
  if (load_model.get_pole_list().size() < 2 || load_model.get_residue_list().size() < 2 || std::abs(driver_slew) <= ista::kTimingTolerance) {
    return true;
  }

  double pole2 = load_model.get_pole_list()[1];
  double residue1 = load_model.get_residue_list()[0];
  double residue2 = load_model.get_residue_list()[1];
  if (!(pole2 > 0.0)) {
    return true;
  }
  double threshold = normalizedThreshold(timing_arc.get_output_threshold_pct_rise());
  double lower_threshold = normalizedThreshold(timing_arc.get_slew_lower_threshold_pct_rise());
  double upper_threshold = normalizedThreshold(timing_arc.get_slew_upper_threshold_pct_rise());
  if (trans_type == TransType::kFall) {
    threshold = normalizedThreshold(timing_arc.get_output_threshold_pct_fall());
    lower_threshold = normalizedThreshold(timing_arc.get_slew_lower_threshold_pct_fall());
    upper_threshold = normalizedThreshold(timing_arc.get_slew_upper_threshold_pct_fall());
  }
  double slew_derate = timing_arc.get_slew_derate();
  if (!(upper_threshold > lower_threshold) || !(slew_derate > 0.0)) {
    return true;
  }

  double residue_pole1 = residue1 / (pole1 * pole1);
  double residue_pole2 = residue2 / (pole2 * pole2);
  double transition_time = driver_slew * slew_derate / (upper_threshold - lower_threshold);
  if (!(transition_time > 0.0))
    return true;
  double threshold_time = loadTime(threshold, pole1, pole2, residue_pole1, residue_pole2, transition_time);
  double lower_time = loadTime(lower_threshold, pole1, pole2, residue_pole1, residue_pole2, transition_time);
  double upper_time = loadTime(upper_threshold, pole1, pole2, residue_pole1, residue_pole2, transition_time);
  double dmp_wire_delay = threshold_time - transition_time * threshold;
  double dmp_load_slew = (upper_time - lower_time) / slew_derate;
  if (std::isfinite(dmp_wire_delay) && std::isfinite(dmp_load_slew) && dmp_load_slew >= 0.0) {
    wire_delay = dmp_wire_delay;
    load_slew = dmp_load_slew;
  }
  return true;
}

bool DmpDelayCalculator::receiverResponse(const ParasiticDmpLoadModel& model, const TimingArc& source, TransType edge, double driver_slew, double threshold,
                                          double lower, double upper, double derate, double& delay, double& slew)
{
  if (!model.get_is_valid() || model.get_pole_list().empty() || !(threshold > 0. && threshold < 1. && lower > 0. && upper < 1. && upper > lower)
      || !std::isfinite(derate) || !(derate > 0.))
    return false;
  const bool fall = edge == TransType::kFall;
  const double source_lower = normalizedThreshold(fall ? source.get_slew_lower_threshold_pct_fall() : source.get_slew_lower_threshold_pct_rise());
  const double source_upper = normalizedThreshold(fall ? source.get_slew_upper_threshold_pct_fall() : source.get_slew_upper_threshold_pct_rise());
  const double source_threshold = normalizedThreshold(fall ? source.get_output_threshold_pct_fall() : source.get_output_threshold_pct_rise());
  const double ramp = std::abs(driver_slew) * source.get_slew_derate() / (source_upper - source_lower);
  if (!std::isfinite(ramp) || ramp < 0. || !(source_upper > source_lower))
    return false;
  const double pole1 = model.get_pole_list().front();
  double pole2 = pole1, residue1 = 1. / pole1, residue2 = 0.;
  if (model.get_pole_list().size() >= 2 && model.get_residue_list().size() >= 2) {
    pole2 = model.get_pole_list()[1];
    residue1 = model.get_residue_list()[0] / (pole1 * pole1);
    residue2 = model.get_residue_list()[1] / (pole2 * pole2);
  }
  if (!(pole1 > 0. && pole2 > 0.) || !std::isfinite(pole1) || !std::isfinite(pole2))
    return false;
  const double middle = loadTime(fall ? 1. - threshold : threshold, pole1, pole2, residue1, residue2, ramp);
  const double early = loadTime(fall ? 1. - upper : lower, pole1, pole2, residue1, residue2, ramp);
  const double late = loadTime(fall ? 1. - lower : upper, pole1, pole2, residue1, residue2, ramp);
  delay = middle - (fall ? 1. - source_threshold : source_threshold) * ramp;
  slew = (late - early) / derate;
  return std::isfinite(delay) && std::isfinite(slew) && slew >= 0.;
}

double DmpDelayCalculator::loadTime(double threshold, double pole1, double pole2, double residue_pole1, double residue_pole2, double transition_time)
{
  auto voltage = [&](double time) {
    if (transition_time == 0.) {
      return 1. - residue_pole1 * pole1 * std::exp(-pole1 * time) - residue_pole2 * pole2 * std::exp(-pole2 * time);
    }
    if (time <= transition_time) {
      return (time + residue_pole1 * std::expm1(-pole1 * time) + residue_pole2 * std::expm1(-pole2 * time)) / transition_time;
    }
    return 1.0
           + (residue_pole1 * std::exp(-pole1 * (time - transition_time)) * std::expm1(-pole1 * transition_time)
              + residue_pole2 * std::exp(-pole2 * (time - transition_time)) * std::expm1(-pole2 * transition_time))
                 / transition_time;
  };
  // Solve the actual waveform, including crossings on either side of the ramp.
  // One Newton step with a dominant-pole estimate is inaccurate for slow ramps.
  double low = 0.0;
  double high = transition_time;
  double value = voltage(high);
  for (int i = 0; i < 64 && std::isfinite(value) && value < threshold; ++i) {
    low = high;
    high = 2.0 * high + 1.0 / std::min(pole1, pole2);
    value = voltage(high);
  }
  if (!std::isfinite(value) || value < threshold)
    return std::numeric_limits<double>::quiet_NaN();
  for (int i = 0; i < 80; ++i) {
    const double mid = low + (high - low) * 0.5;
    if (mid == low || mid == high)
      break;
    value = voltage(mid);
    if (!std::isfinite(value))
      return std::numeric_limits<double>::quiet_NaN();
    if (value < threshold)
      low = mid;
    else
      high = mid;
  }
  return low + (high - low) * 0.5;
}

}  // namespace ista
