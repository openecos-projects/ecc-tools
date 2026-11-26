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
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

#include "ArnoldiWaveformSolver.hpp"
#include "ThresholdNormalizer.hpp"

namespace ista {
namespace {
const double kRelativeTolerance = std::sqrt(std::numeric_limits<double>::epsilon());
constexpr int kIterationLimit = 2 * std::numeric_limits<double>::digits;

struct DriverFit
{
  double resistance;
  double ramp;
  double start;
  bool delay_identified;
};

// A linear source ramp behind R drives C dv/dt = (u-v)/R. The measured
// transition bounds R by the step-response span, R*C*log(upper/lower).
// For each R, the slew determines the ramp; the local library load sensitivity
// then determines R. This removes the unknown time origin
// from the scalar solve. All voltage arguments describe the remaining swing.
std::optional<DriverFit> fitDriver(double capacitance, double delay, double sensitivity, double slew,
                                   double lower, double upper, double threshold, double plateau_factor)
{
  if (!(capacitance > 0. && slew > 0.) || !std::isfinite(delay) || !std::isfinite(sensitivity) || !std::isfinite(slew))
    return std::nullopt;
  const double bound = (slew / std::log(upper / lower)) / capacitance;
  if (!(bound > 0.) || !std::isfinite(bound))
    return std::nullopt;
  const double target = sensitivity;
  const auto ramp_at = [&](double resistance) {
    double ramp = 0.;
    ArnoldiWaveformSolver::solveRamp(1. / (resistance * capacitance), slew, lower, upper, ramp);
    return ramp;
  };
  const auto load_sensitivity = [&](double resistance, double ramp) {
    return resistance * ArnoldiWaveformSolver::rampTimeSensitivity(1. / (resistance * capacitance), ramp, threshold);
  };

  // A nonpositive or unattainable delay sensitivity does not identify this
  // two-parameter source. Use the slew-limited RC model in that case. The
  // plateau endpoint is determined by the upper voltage threshold, so it has
  // the same measured span as the ideal step and a finite charge window.
  double resistance = bound;
  double ramp = plateau_factor * resistance * capacitance;
  // Delay sensitivity need not be monotone for asymmetric thresholds: a
  // crossing can move through the end of the ramp. Find the first sign
  // change before bisection, selecting the lowest-resistance solution when
  // the observations admit multiple sources. An unbracketed fit uses the
  // explicitly defined slew limit above.
  double low = 0., high = 0.;
  if (target > 0.) {
    constexpr int kBracketIntervals = 64;
    for (int interval = 1; interval <= kBracketIntervals; ++interval) {
      const double candidate = bound * (static_cast<double>(interval) / kBracketIntervals);
      const double candidate_ramp = interval == kBracketIntervals ? ramp : ramp_at(candidate);
      if (!(candidate_ramp >= 0.) || !std::isfinite(candidate_ramp))
        return std::nullopt;
      if (load_sensitivity(candidate, candidate_ramp) >= target) {
        high = candidate;
        break;
      }
      low = candidate;
    }
  }
  const bool delay_identified = high > low;
  if (delay_identified) {
    bool converged = false;
    for (int iteration = 0; iteration < kIterationLimit; ++iteration) {
      resistance = std::midpoint(low, high);
      ramp = ramp_at(resistance);
      if (!(ramp >= 0.) || !std::isfinite(ramp))
        return std::nullopt;
      const double residual = load_sensitivity(resistance, ramp) - target;
      if (!std::isfinite(residual))
        return std::nullopt;
      if (std::abs(residual) <= 32. * std::numeric_limits<double>::epsilon() * std::max(bound, target)
          || std::nextafter(low, high) == high) {
        converged = true;
        break;
      }
      if (residual > 0.)
        high = resistance;
      else
        low = resistance;
    }
    if (!converged)
      return std::nullopt;
  }
  if (ramp == 0.)
    ramp = plateau_factor * resistance * capacitance;
  const double crossing = ArnoldiWaveformSolver::rampTime(1. / (resistance * capacitance), ramp, threshold);
  if (!(ramp > 0.) || !std::isfinite(ramp) || !std::isfinite(crossing))
    return std::nullopt;
  return DriverFit{resistance, ramp, delay - crossing, delay_identified};
}

bool validModel(const ParasiticArnoldiModel& model)
{
  if (!model.get_is_valid() || model.get_order() <= 0 || !(model.get_total_capacitance() > 0.)
      || !std::isfinite(model.get_total_capacitance()) || !(model.get_sqrt_total_capacitance() > 0.)
      || !std::isfinite(model.get_sqrt_total_capacitance()))
    return false;
  const auto order = static_cast<std::size_t>(model.get_order());
  const auto terms = model.get_term_node_list().size();
  if (terms == 0 || model.get_diagonal_list().size() != order || model.get_off_diagonal_list().size() + 1 != order
      || model.get_projection_list().size() != order)
    return false;
  for (const auto& row : model.get_projection_list()) {
    if (row.size() != terms || !std::all_of(row.begin(), row.end(), [](double value) { return std::isfinite(value); }))
      return false;
  }
  return true;
}
}  // namespace

std::optional<ArnoldiDelayResult> ArnoldiDelayCalculator::calculate(TimingArc& timing_arc, TransType edge, double input_slew,
                                                                   const ParasiticArnoldiModel& model) const
{
  if (!validModel(model) || !(input_slew >= 0.) || !std::isfinite(input_slew))
    return std::nullopt;
  const auto delay_table = timing_arc.get_delay_table_map().find(edge);
  const auto slew_table = timing_arc.get_slew_table_map().find(edge);
  if (delay_table == timing_arc.get_delay_table_map().end() || slew_table == timing_arc.get_slew_table_map().end()
      || delay_table->second.get_value_list().empty() || slew_table->second.get_value_list().empty())
    return std::nullopt;
  const double time_scale = timing_arc.get_time_unit_scale(), cap_scale = timing_arc.get_cap_unit_scale();
  if (!(time_scale > 0. && cap_scale > 0.) || !std::isfinite(time_scale) || !std::isfinite(cap_scale))
    return std::nullopt;
  const bool falling = edge == TransType::kFall;
  double lower = normalizedThreshold(falling ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise());
  double upper = normalizedThreshold(falling ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise());
  double threshold = normalizedThreshold(falling ? timing_arc.get_output_threshold_pct_fall() : timing_arc.get_output_threshold_pct_rise());
  const double derate = timing_arc.get_slew_derate();
  if (!(lower > 0. && upper < 1. && upper > lower && threshold > 0. && threshold < 1. && derate > 0.) || !std::isfinite(derate))
    return std::nullopt;
  if (!falling) {
    const double previous_lower = lower;
    lower = 1. - upper;
    upper = 1. - previous_lower;
    threshold = 1. - threshold;
  }

  // The longest ramp whose upper crossing is at the ramp end satisfies
  // (1-exp(-x))/x = upper. It is a geometry/threshold limit, independent of
  // a particular library, technology, or timing scale.
  double plateau_low = 0., plateau_high = 1. / upper;
  for (int iteration = 0; iteration < kIterationLimit; ++iteration) {
    const double point = std::midpoint(plateau_low, plateau_high);
    if (point == plateau_low || point == plateau_high)
      break;
    if (-std::expm1(-point) / point > upper)
      plateau_low = point;
    else
      plateau_high = point;
  }
  const double plateau_factor = std::midpoint(plateau_low, plateau_high);
  const double total = model.get_total_capacitance();
  struct Evaluation
  {
    double capacitance;
    double residual;
    DriverFit driver;
    ParasiticArnoldiPoleResidue waveform;
  };
  const double nominal_slew = derate * tableSlew(timing_arc, edge, input_slew, total);
  const double nominal_sensitivity = tableSensitivity(timing_arc, edge, input_slew, total);
  const double slew_bound = nominal_slew / (total * std::log(upper / lower));
  if (!(slew_bound > 0.) || !std::isfinite(slew_bound) || !std::isfinite(nominal_sensitivity))
    return std::nullopt;
  // In the slow-ramp limit the load delay is R*C, so the local delay slope
  // estimates R. Bound it by the observed RC slew. With nonpositive delay
  // sensitivity, explicitly use that boundary as an approximate source.
  // Keep this approximate source fixed throughout each restarted solve.
  const double fixed_resistance = nominal_sensitivity > 0. ? std::min(nominal_sensitivity, slew_bound) : slew_bound;
  const auto fixed_driver = [&](double capacitance, double delay, double slew) -> std::optional<DriverFit> {
    if (!(slew > 0.) || !std::isfinite(delay) || !std::isfinite(slew))
      return std::nullopt;
    const double tau = fixed_resistance * capacitance;
    double ramp = 0.;
    if (slew > tau * std::log(upper / lower))
      ArnoldiWaveformSolver::solveRamp(1. / tau, slew, lower, upper, ramp);
    // An extrapolated table can demand a transition faster than this fixed
    // resistance permits. Use its minimum RC span, with a finite charge window.
    if (ramp == 0.)
      ramp = plateau_factor * tau;
    const double crossing = ArnoldiWaveformSolver::rampTime(1. / tau, ramp, threshold);
    if (!(ramp > 0.) || !std::isfinite(ramp) || !std::isfinite(crossing))
      return std::nullopt;
    return DriverFit{fixed_resistance, ramp, delay - crossing, false};
  };
  enum class LoadEquation { kCharacterizedCrossing, kFixedCrossing, kFixedCharge };
  const auto solve_load = [&](LoadEquation equation) -> std::optional<Evaluation> {
    const bool fixed_source = equation != LoadEquation::kCharacterizedCrossing;
    const bool charge_only = equation == LoadEquation::kFixedCharge;
    const auto fixed_waveform = fixed_source ? ArnoldiWaveformSolver::poleResidue(model, fixed_resistance) : ParasiticArnoldiPoleResidue{};
    if (fixed_source && !fixed_waveform.get_is_valid())
      return std::nullopt;
    const auto evaluate = [&](double capacitance) -> std::optional<Evaluation> {
      const double delay = tableDelay(timing_arc, edge, input_slew, capacitance);
      const double slew = derate * tableSlew(timing_arc, edge, input_slew, capacitance);
      const auto driver = fixed_source ? fixed_driver(capacitance, delay, slew)
                                     : fitDriver(capacitance, delay, tableSensitivity(timing_arc, edge, input_slew, capacitance),
                                                 slew, lower, upper, threshold, plateau_factor);
      if (!driver || (!fixed_source && !driver->delay_identified))
        return std::nullopt;
      // A joint evaluation owns its resistance and modal response. Each fixed
      // source uses one conductance matrix throughout its iteration.
      auto waveform = fixed_source ? fixed_waveform : ArnoldiWaveformSolver::poleResidue(model, driver->resistance);
      if (!waveform.get_is_valid())
        return std::nullopt;
      double matched = 0.;
      if (!charge_only) {
        double crossing = 0.;
        ArnoldiWaveformSolver::crossingTime(driver->ramp, waveform, 0, threshold, crossing);
        const auto capacitance_at_crossing = ArnoldiWaveformSolver::crossingCapacitance(driver->ramp, driver->resistance, total, crossing, threshold);
        if (!capacitance_at_crossing)
          return std::nullopt;
        matched = *capacitance_at_crossing;
      } else {
        matched = ArnoldiWaveformSolver::effectiveCapacitance(driver->ramp, driver->resistance, waveform, driver->ramp);
      }
      if (!(matched > 0.) || !std::isfinite(matched))
        return std::nullopt;
      return Evaluation{capacitance, std::min(matched, total) - capacitance, *driver, std::move(waveform)};
    };
    const auto converged = [](const Evaluation& state) {
      return std::abs(state.residual) <= kRelativeTolerance * state.capacitance;
    };
    auto state = evaluate(total);
    if (!state || converged(*state))
      return state;
    double low = 0., high = total;
    if (state->residual > 0.)
      return std::nullopt;
    // A fixed-source root may lie above a nonpositive-slew extrapolation. Locate
    // its feasible lower side without accepting a jump as a residual zero.
    bool bracketed = false;
    for (int iteration = 0; iteration < kIterationLimit; ++iteration) {
      const double capacitance = std::midpoint(low, high);
      auto trial = evaluate(capacitance);
      if (!trial) {
        if (!fixed_source || bracketed)
          return std::nullopt;
        low = capacitance;
        continue;
      }
      state = std::move(trial);
      if (converged(*state))
        return state;
      if (state->residual > 0.) {
        low = capacitance;
        bracketed = true;
      } else
        high = capacitance;
      if (std::nextafter(low, high) == high)
        break;
    }
    return std::nullopt;
  };
  auto state = solve_load(LoadEquation::kCharacterizedCrossing);
  // A piecewise-linear LUT has discontinuous local sensitivities. If its
  // joint model has no root, restart with a fixed resistance and match the
  // source crossing. Charge equivalence is the last fallback when crossing
  // equivalence has no feasible root. Each restart uses one residual;
  // never mix definitions or publish a partially converged joint waveform.
  if (!state)
    state = solve_load(LoadEquation::kFixedCrossing);
  if (!state)
    state = solve_load(LoadEquation::kFixedCharge);
  if (!state)
    return std::nullopt;

  std::vector<double> delays, slews;
  for (std::size_t term = 0; term < model.get_term_node_list().size(); ++term) {
    double early = 0., middle = 0., late = 0.;
    ArnoldiWaveformSolver::crossingTime(state->driver.ramp, state->waveform, term, upper, early, threshold, middle, lower, late);
    if (!std::isfinite(early) || !std::isfinite(middle) || !std::isfinite(late) || late < early)
      return std::nullopt;
    delays.push_back(middle + state->driver.start);
    slews.push_back((late - early) / derate);
  }
  return ArnoldiDelayResult{state->capacitance, state->driver.resistance, std::move(delays), std::move(slews),
                           state->driver.ramp, std::move(state->waveform)};
}

double ArnoldiDelayCalculator::tableDelay(TimingArc& timing_arc, TransType edge, double input_slew, double output_load)
{
  const double delay = timing_arc.get_delay_table_map().at(edge).findValue(input_slew * timing_arc.get_time_unit_scale(),
                                                                         output_load * timing_arc.get_cap_unit_scale());
  return delay / timing_arc.get_time_unit_scale();
}

double ArnoldiDelayCalculator::tableSlew(TimingArc& timing_arc, TransType edge, double input_slew, double output_load)
{
  const double slew = timing_arc.get_slew_table_map().at(edge).findValue(input_slew * timing_arc.get_time_unit_scale(),
                                                                       output_load * timing_arc.get_cap_unit_scale());
  return slew / timing_arc.get_time_unit_scale();
}

double ArnoldiDelayCalculator::tableSensitivity(TimingArc& timing_arc, TransType edge, double input_slew, double output_load)
{
  auto& table = timing_arc.get_delay_table_map().at(edge);
  const auto& axes = table.get_axis_list();
  const std::size_t axis_index = table.get_variable_type1() == TimingTableVariableType::kOutputCapacitance ? 0 : 1;
  if ((axis_index == 1 && table.get_variable_type2() != TimingTableVariableType::kOutputCapacitance)
      || axis_index >= axes.size() || axes[axis_index].size() < 2)
    return 0.;
  const auto& axis = axes[axis_index];
  const double cap_scale = timing_arc.get_cap_unit_scale();
  // Match findValue's right-hand segment at an interior knot and its end
  // segments for extrapolation. No sample distance or perturbation is needed.
  auto end = std::upper_bound(axis.begin(), axis.end(), output_load * cap_scale);
  if (end == axis.begin())
    ++end;
  if (end == axis.end())
    --end;
  const double high = *end, low = *(end - 1);
  if (std::abs(high - low) < ista::kTimingTolerance)
    return 0.;
  const double slew = input_slew * timing_arc.get_time_unit_scale();
  return (table.findValue(slew, high) - table.findValue(slew, low)) / (high - low)
         * cap_scale / timing_arc.get_time_unit_scale();
}
}  // namespace ista
