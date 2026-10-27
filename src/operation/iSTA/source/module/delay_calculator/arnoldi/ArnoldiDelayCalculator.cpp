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
#include "ArnoldiDelayCalculator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "ArnoldiWaveformSolver.hpp"
#include "TimingTolerance.hpp"

namespace ista {
namespace {
struct DriverFit
{
  double resistance;
  double ramp;
  double start;
};

// Rising response of a ramp source driving one capacitance through R.
double driverVoltage(double time, double tau, double ramp)
{
  if (time <= 0.)
    return 0.;
  if (time <= ramp)
    return (time + tau * std::expm1(-time / tau)) / ramp;
  return 1. - tau / ramp * (-std::expm1(-ramp / tau)) * std::exp(-(time - ramp) / tau);
}

// Start time and resistance both have decreasing voltage residuals. Retain
// the bracket when a Newton step leaves the physical interval.
template <class Residual, class Derivative>
bool solveDriverParameter(double& value, double lower, double upper, Residual residual, Derivative derivative)
{
  if (!(lower < upper) || residual(lower) < 0. || residual(upper) > 0.)
    return false;
  value = std::clamp(value, lower, upper);
  for (int iteration = 0; iteration < 50; ++iteration) {
    const double error = residual(value);
    if (!std::isfinite(error))
      return false;
    if (std::abs(error) < .001)
      return true;
    if (error > 0.)
      lower = value;
    else
      upper = value;
    const double next = value - error / derivative(value);
    value = std::isfinite(next) && next > lower && next < upper ? next : lower + .5 * (upper - lower);
  }
  return false;
}

// Fit a ramp source and series resistance to two neighboring delay samples
// and one physical slew. A flat delay slope alone cannot identify the driver
// waveform. Use bounded alternating solves; reject inconsistent table data.
std::optional<DriverFit> fitDriver(double capacitance, double delay, double next_delay, double slew, double lower, double upper)
{
  const double voltage_log = std::log(upper / lower);
  const double bound = slew / (voltage_log * capacitance);
  const double next_capacitance = 1.01 * capacitance;
  if (!(capacitance > 0. && slew > 0. && bound > 0.) || !std::isfinite(bound) || !std::isfinite(delay) || !std::isfinite(next_delay) || next_delay < delay
      || next_delay - delay > .01 * capacitance * bound)
    return std::nullopt;
  double resistance = .9 * bound;
  const double span = slew / (upper - lower);
  double ramp = .5 * span;
  double start = delay - .5 * span;
  double tolerance = .010001;
  for (int iteration = 0; iteration < 50; ++iteration) {
    if (iteration > 0 && iteration % 10 == 0)
      tolerance *= 2.;
    if (resistance >= bound)
      resistance = .9 * bound;
    const double tau = resistance * capacitance;
    double ramp_lower = 0., ramp_upper = span;
    double ramp_tolerance = .001;
    bool ramp_converged = false;
    for (int trial = 0; trial < 50; ++trial) {
      const double actual = ArnoldiWaveformSolver::rampTime(1. / tau, ramp, lower) - ArnoldiWaveformSolver::rampTime(1. / tau, ramp, upper);
      const double error = actual / slew - 1.;
      if (std::abs(error) < ramp_tolerance) {
        ramp_converged = true;
        break;
      }
      if (error > 0.)
        ramp_upper = ramp;
      else
        ramp_lower = ramp;
      ramp = ramp_lower + .5 * (ramp_upper - ramp_lower);
      if ((trial + 1) % 10 == 0)
        ramp_tolerance *= 2.;
    }
    if (!ramp_converged || !(ramp > 0.) || !std::isfinite(ramp))
      return std::nullopt;
    // Select the derivative from the segment containing the target crossing.
    // Switching segments at each trial gives a different stopping point near
    // the ramp end, which can change the later network-limited decision.
    const bool crossing_during_ramp = driverVoltage(ramp, tau, ramp) >= .5;
    const auto start_residual = [&](double value) { return 2. * driverVoltage(delay - value, tau, ramp) - 1.; };
    const auto start_derivative = [&](double value) {
      const double time = delay - value;
      return crossing_during_ramp ? 2. * std::expm1(-time / tau) / ramp : 2. * (std::exp(-time / tau) - std::exp(-(time - ramp) / tau)) / ramp;
    };
    if (!solveDriverParameter(start, delay - span, delay, start_residual, start_derivative))
      return std::nullopt;
    const double target = next_delay - start;
    const auto resistance_residual = [&](double value) { return 2. * driverVoltage(target, value * next_capacitance, ramp) - 1.; };
    const auto resistance_derivative = [&](double value) {
      const double next_tau = value * next_capacitance;
      const double end = driverVoltage(ramp, next_tau, ramp) >= .5 ? 0. : target - ramp;
      return 2. * next_capacitance / ramp * ((1. + target / next_tau) * std::exp(-target / next_tau) - (1. + end / next_tau) * std::exp(-end / next_tau));
    };
    const double maximum_resistance = (delay - start) / (std::log(2.) * capacitance);
    if (!solveDriverParameter(resistance, 1e-5, maximum_resistance, resistance_residual, resistance_derivative))
      return std::nullopt;
    const double pole = 1. / (resistance * capacitance);
    const double actual_slew = ArnoldiWaveformSolver::rampTime(pole, ramp, lower) - ArnoldiWaveformSolver::rampTime(pole, ramp, upper);
    const double residual = std::abs(actual_slew / slew - 1.) + 2. * std::abs(driverVoltage(delay - start, resistance * capacitance, ramp) - .5)
                            + std::abs(resistance_residual(resistance));
    if (residual <= tolerance && resistance < bound)
      return DriverFit{resistance, ramp, start};
  }
  return std::nullopt;
}
}  // namespace

std::optional<ArnoldiDelayResult> ArnoldiDelayCalculator::calculate(TimingArc& timing_arc, TransType output_trans_type, double input_slew,
                                                                    const ParasiticArnoldiModel& arnoldi_model, bool adjust_drive_resistance) const
{
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0 || arnoldi_model.get_total_capacitance() <= 0.0) {
    return std::nullopt;
  }
  double total_capacitance = arnoldi_model.get_total_capacitance();
  double delay_at_total_capacitance = tableDelay(timing_arc, output_trans_type, input_slew, total_capacitance);
  double delay_at_half_capacitance = tableDelay(timing_arc, output_trans_type, input_slew, 0.5 * total_capacitance);
  double delay_resistance = 0.0;
  if (total_capacitance > ista::kTimingTolerance) {
    delay_resistance = (delay_at_total_capacitance - delay_at_half_capacitance) / (0.5 * total_capacitance);
  }
  if (!(delay_resistance > 0.0)) {
    delay_resistance = 1.0;
  }

  double slew_derate = 1.0;
  double lower_threshold = 0.1;
  double upper_threshold = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  ArnoldiWaveformSolver::thresholds(timing_arc, output_trans_type, slew_derate, lower_threshold, upper_threshold, voltage_log, min_slew_factor, x1, y1);

  double output_threshold = output_trans_type == TransType::kRise ? timing_arc.get_output_threshold_pct_rise() : timing_arc.get_output_threshold_pct_fall();
  if (output_threshold > 1.)
    output_threshold *= .01;
  const bool supported_thresholds
      = lower_threshold < .5 && upper_threshold > .5 && std::abs(lower_threshold + upper_threshold - 1.) < 1e-12 && std::abs(output_threshold - .5) < 1e-12;
  auto fit_table = [&](double capacitance) -> std::optional<DriverFit> {
    if (!supported_thresholds)
      return std::nullopt;
    return fitDriver(capacitance, tableDelay(timing_arc, output_trans_type, input_slew, capacitance),
                     tableDelay(timing_arc, output_trans_type, input_slew, 1.01 * capacitance),
                     slew_derate * tableSlew(timing_arc, output_trans_type, input_slew, capacitance), lower_threshold, upper_threshold);
  };
  double drive_resistance = tableResistance(timing_arc, output_trans_type, input_slew, total_capacitance, voltage_log, slew_derate, delay_resistance);
  if (!(drive_resistance > 0.0 && drive_resistance < 100.0)) {
    drive_resistance = 1.0;
  }
  double driver_ramp = tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, total_capacitance, lower_threshold, upper_threshold, voltage_log,
                                 slew_derate, min_slew_factor);
  if (!(driver_ramp > 0.0 && driver_ramp < 100.0)) {
    driver_ramp = 0.5;
  }

  const double charge_resistance = drive_resistance;
  const double charge_ramp = driver_ramp;
  auto driver_fit = fit_table(total_capacitance);
  const bool joint_fit = driver_fit.has_value();
  if (driver_fit) {
    drive_resistance = driver_fit->resistance;
    driver_ramp = driver_fit->ramp;
  }

  ParasiticArnoldiPoleResidue pole_residue = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
  if (!pole_residue.get_is_valid()) {
    return std::nullopt;
  }

  double effective_capacitance = total_capacitance;
  bool request_adjustment = false;
  auto network_limited = [&]() {
    if (!adjust_drive_resistance || !supported_thresholds)
      return false;
    double midpoint = 0.;
    ArnoldiWaveformSolver::crossingTime(driver_ramp, pole_residue, 0, .5, midpoint);
    double step_voltage = 0.;
    for (std::size_t pole = 0; pole < pole_residue.get_pole_list().size(); ++pole)
      step_voltage += pole_residue.get_residue_list().front()[pole] * std::exp(-pole_residue.get_pole_list()[pole] * midpoint);
    return std::isfinite(midpoint) && std::isfinite(step_voltage) && 2. * std::abs(step_voltage - .5) < .45;
  };
  auto fit_load = [&](bool match_crossing) {
    double lower_capacitance = 0.;
    double upper_capacitance = std::numeric_limits<double>::infinity();
    bool converged = false;
    // Fit the distributed source crossing for both ordinary and adjusted
    // two-load drivers. Charge matching remains the fallback for table data
    // that cannot identify a consistent two-load driver waveform.
    for (int32_t iter = 0; iter < 64; ++iter) {
      // Adjustment can be needed at total C or during a later Ceff trial.
      // Testing only the final waveform can miss the branch altogether.
      if (!match_crossing && network_limited()) {
        request_adjustment = true;
        return false;
      }
      double capacitance = 0.;
      if (match_crossing || driver_fit) {
        double source_time = 0.;
        ArnoldiWaveformSolver::crossingTime(driver_ramp, pole_residue, 0, .5, source_time);
        const double reference = ArnoldiWaveformSolver::rampTime(1. / (drive_resistance * effective_capacitance), driver_ramp, .5);
        const double nominal = tableDelay(timing_arc, output_trans_type, input_slew, effective_capacitance);
        const double actual = source_time + (driver_fit ? driver_fit->start : nominal - reference);
        // Accept the fitted model by its delay error before another Ceff update
        // can distort it. Keep this same waveform intact for every receiver.
        // Internal time is ns: 1% of 25 ps supplies a 0.25 ps absolute floor.
        if (std::abs(actual - nominal) < .01 * std::max(std::abs(actual), .025)) {
          converged = true;
          break;
        }
        const auto matched = ArnoldiWaveformSolver::crossingCapacitance(driver_ramp, drive_resistance, total_capacitance, source_time, .5);
        if (!matched)
          return false;
        capacitance = *matched;
      } else {
        capacitance = ArnoldiWaveformSolver::effectiveCapacitance(driver_ramp, drive_resistance, pole_residue, driver_ramp);
      }
      if (!std::isfinite(capacitance) || capacitance <= 0.0) {
        return false;
      }
      const auto next_fit = !match_crossing && driver_fit ? fit_table(capacitance) : std::nullopt;
      if (!match_crossing && driver_fit && !next_fit)
        return false;
      const double ramp = next_fit ? next_fit->ramp
                                   : tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, capacitance, lower_threshold, upper_threshold,
                                               voltage_log, slew_derate, min_slew_factor);
      if (!std::isfinite(ramp) || ramp <= 0.0)
        return false;
      converged = !match_crossing && std::abs(capacitance - effective_capacitance) <= 1e-7 * std::max(capacitance, effective_capacitance)
                  && std::abs(ramp - driver_ramp) <= 1e-7 * std::max(ramp, driver_ramp);
      if (converged) {
        effective_capacitance = capacitance;
        driver_ramp = ramp;
        if (next_fit) {
          driver_fit = next_fit;
          drive_resistance = next_fit->resistance;
          pole_residue = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
        }
        break;
      }
      // Solve matched(C) - C = 0. Nonlinear tables can make plain fixed-point
      // iteration oscillate around a valid solution. Retain the residual-sign
      // bracket and switch to bisection when iteration stops making progress.
      if (capacitance > effective_capacitance)
        lower_capacitance = effective_capacitance;
      else
        upper_capacitance = effective_capacitance;
      if (std::isfinite(upper_capacitance) && (iter >= 8 || capacitance <= lower_capacitance || capacitance >= upper_capacitance)) {
        effective_capacitance = lower_capacitance + .5 * (upper_capacitance - lower_capacitance);
      } else {
        effective_capacitance = capacitance;
      }
      if (!match_crossing && driver_fit) {
        driver_fit = effective_capacitance == capacitance ? next_fit : fit_table(effective_capacitance);
        if (!driver_fit)
          return false;
        drive_resistance = driver_fit->resistance;
        driver_ramp = driver_fit->ramp;
        pole_residue = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
        if (!pole_residue.get_is_valid())
          return false;
      } else {
        driver_ramp = tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, effective_capacitance, lower_threshold, upper_threshold,
                                voltage_log, slew_derate, min_slew_factor);
        if (!(driver_ramp > 0.) || !std::isfinite(driver_ramp))
          return false;
      }
    }
    return converged;
  };
  bool converged = fit_load(false);
  if (!converged && joint_fit && !request_adjustment) {
    // Shielding may enter a locally inconsistent LUT segment even when the
    // initial two-load fit was feasible. Restart the charge model from its
    // original state; never reuse a partial joint fit as a valid waveform.
    driver_fit.reset();
    drive_resistance = charge_resistance;
    driver_ramp = charge_ramp;
    effective_capacitance = total_capacitance;
    pole_residue = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
    if (!pole_residue.get_is_valid())
      return std::nullopt;
    converged = fit_load(false);
  }
  bool resistance_adjusted = false;
  if (request_adjustment) {
    const double transition = slew_derate * tableSlew(timing_arc, output_trans_type, input_slew, total_capacitance);
    const double adjusted
        = (upper_threshold - .5) * transition / ((upper_threshold - lower_threshold) * std::log(.5 / (1. - upper_threshold)) * total_capacitance);
    if (!(adjusted > 0.) || !std::isfinite(adjusted))
      return std::nullopt;
    driver_fit.reset();
    drive_resistance = adjusted;
    effective_capacitance = total_capacitance;
    driver_ramp = tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, total_capacitance, lower_threshold, upper_threshold, voltage_log,
                            slew_derate, min_slew_factor);
    pole_residue = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
    if (!pole_residue.get_is_valid() || !(driver_ramp > 0.) || !std::isfinite(driver_ramp))
      return std::nullopt;
    // The adjusted driver matches its lumped table delay to the distributed
    // source crossing; receiver delay and slew use this same fitted waveform.
    converged = fit_load(true);
    resistance_adjusted = converged;
  }
  if (!converged)
    return std::nullopt;

  double table_delay = tableDelay(timing_arc, output_trans_type, input_slew, effective_capacitance);
  double reference_time = ArnoldiWaveformSolver::rampTime(1.0 / (drive_resistance * effective_capacitance), driver_ramp, 0.5);
  std::vector<double> delay_list;
  std::vector<double> slew_list;
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    double upper_time = 0.0;
    double middle_time = 0.0;
    double lower_time = 0.0;
    ArnoldiWaveformSolver::crossingTime(driver_ramp, pole_residue, term_idx, upper_threshold, upper_time, 0.5, middle_time, lower_threshold, lower_time);
    delay_list.push_back(middle_time + (driver_fit ? driver_fit->start : table_delay - reference_time));
    slew_list.push_back((lower_time - upper_time) / slew_derate);
  }
  if (delay_list.empty() || slew_list.empty()) {
    return std::nullopt;
  }

  return ArnoldiDelayResult{effective_capacitance, drive_resistance,        std::move(delay_list), std::move(slew_list),
                            driver_ramp,           std::move(pole_residue), resistance_adjusted};
}

double ArnoldiDelayCalculator::tableResistance(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double total_capacitance,
                                               double voltage_log, double slew_derate, double delay_resistance)
{
  if (!(total_capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = tableSlew(timing_arc, output_trans_type, input_slew, total_capacitance);
  double transition_time = slew_derate * table_slew;
  double drive_resistance = transition_time / (voltage_log * total_capacitance);
  if (delay_resistance > 0.0 && drive_resistance > delay_resistance) {
    drive_resistance = delay_resistance;
  }
  return drive_resistance;
}

double ArnoldiDelayCalculator::tableRamp(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double drive_resistance, double capacitance,
                                         double lower_threshold, double upper_threshold, double voltage_log, double slew_derate, double min_slew_factor)
{
  if (!(drive_resistance > 0.0 && capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = tableSlew(timing_arc, output_trans_type, input_slew, capacitance);
  double transition_time = slew_derate * table_slew;
  double min_slew = drive_resistance * capacitance * min_slew_factor;
  if (voltage_log * drive_resistance * capacitance >= transition_time) {
    return min_slew;
  }
  double ramp = min_slew + 0.3 * transition_time;
  ArnoldiWaveformSolver::solveRamp(1.0 / (drive_resistance * capacitance), transition_time, lower_threshold, upper_threshold, ramp);
  return ramp;
}

double ArnoldiDelayCalculator::tableDelay(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return delay / timing_arc.get_time_unit_scale();
}

double ArnoldiDelayCalculator::tableSlew(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return slew / timing_arc.get_time_unit_scale();
}

}  // namespace ista
