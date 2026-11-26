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
#include "ArnoldiWaveformSolver.hpp"

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "TimingTolerance.hpp"

namespace ista {
namespace {
// Integral from 0 to x of (1 - exp(-u)). Direct subtraction loses the
// charge of slow modes when their time constant greatly exceeds the ramp.
double integratedDecay(double x)
{
  if (x < 1e-3) {
    return x * x * (.5 + x * (-1. / 6. + x * (1. / 24. + x * (-1. / 120. + x / 720.))));
  }
  return x + std::expm1(-x);
}

// Mean exponential response over a ramp. The limit at zero avoids 0/0
// when a positive rate times a positive duration underflows.
double meanDecay(double x)
{
  return x == 0. ? 1. : -std::expm1(-x) / x;
}

// Newton accelerates a sign-changing bracket; bisection supplies progress
// when the derivative vanishes or the Newton step leaves the interval.
// The stopping criterion is relative to the root's units and double precision.
template <typename Function>
double bracketedRoot(Function&& evaluate, double low, double high, double initial = std::numeric_limits<double>::quiet_NaN())
{
  const double failure = std::numeric_limits<double>::quiet_NaN();
  if (!std::isfinite(low) || !std::isfinite(high) || high < low)
    return failure;
  double low_value = evaluate(low).first;
  const double high_value = evaluate(high).first;
  if (!std::isfinite(low_value) || !std::isfinite(high_value))
    return failure;
  if (low_value == 0.)
    return low;
  if (high_value == 0.)
    return high;
  if (std::signbit(low_value) == std::signbit(high_value))
    return failure;

  double point = std::isfinite(initial) && initial > low && initial < high ? initial : std::midpoint(low, high);
  double previous_step = high - low;
  for (int iteration = 0; iteration < 4 * std::numeric_limits<double>::digits; ++iteration) {
    const auto [value, derivative] = evaluate(point);
    if (!std::isfinite(value))
      return failure;
    if (value == 0.)
      return point;
    if (std::signbit(value) == std::signbit(low_value)) {
      low = point;
      low_value = value;
    } else {
      high = point;
    }
    const double midpoint = std::midpoint(low, high);
    const double tolerance = 8. * std::numeric_limits<double>::epsilon() * std::max(std::abs(low), std::abs(high));
    if (midpoint == low || midpoint == high || high - low <= tolerance)
      return midpoint;

    double next = midpoint;
    if (std::isfinite(derivative) && derivative != 0.) {
      const double candidate = point - value / derivative;
      if (std::isfinite(candidate) && candidate > low && candidate < high && std::abs(candidate - point) < .5 * previous_step)
        next = candidate;
    }
    previous_step = std::abs(next - point);
    point = next;
  }
  return failure;
}
}  // namespace

std::optional<InputPortDelayResult> ArnoldiWaveformSolver::inputPortResponse(const ParasiticArnoldiModel& model, TransType transition, double input_slew,
                                                                             const WaveformThresholds& source, const std::vector<WaveformThresholds>& loads)
{
  const auto term_count = model.get_term_node_list().size();
  const auto model_order = model.get_order();
  if (!model.get_is_valid() || model_order <= 0 || !std::isfinite(input_slew) || input_slew < 0.0 || loads.size() != term_count
      || model.get_diagonal_list().size() < static_cast<std::size_t>(model_order)
      || model.get_off_diagonal_list().size() < static_cast<std::size_t>(model_order - 1)
      || model.get_projection_list().size() < static_cast<std::size_t>(model_order)) {
    return std::nullopt;
  }
  for (int32_t basis = 0; basis < model_order; ++basis) {
    if (model.get_projection_list()[basis].size() < term_count) {
      return std::nullopt;
    }
  }
  if (!(source.lower > 0.0 && source.upper < 1.0 && source.upper > source.lower && source.derate > 0.0 && source.delay > 0.0 && source.delay < 1.0)) {
    return std::nullopt;
  }
  // The port imposes a voltage waveform. Use zero source resistance and the
  // reduced RC response, retaining the fast modes lost by an Elmore estimate.
  int32_t order = model.get_order();
  Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(order, order);
  for (int32_t index = 0; index < order; ++index) {
    matrix(index, index) = model.get_diagonal_list()[index];
    if (index + 1 < order) {
      matrix(index, index + 1) = matrix(index + 1, index) = model.get_off_diagonal_list()[index];
    }
  }
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(matrix);
  if (solver.info() != Eigen::Success || !solver.eigenvalues().allFinite()) {
    return std::nullopt;
  }
  std::vector<double> time_constants(order);
  double scale = solver.eigenvalues().cwiseAbs().maxCoeff();
  for (int32_t index = 0; index < order; ++index) {
    double value = solver.eigenvalues()[index];
    if (value < -scale * 1E-12) {
      return std::nullopt;
    }
    time_constants[index] = value > scale * 1E-12 ? value : 0.0;
  }
  const bool is_fall = transition == TransType::kFall;
  double ramp = input_slew * source.derate / (source.upper - source.lower);
  double source_time = (is_fall ? 1.0 - source.delay : source.delay) * ramp;
  InputPortDelayResult result;
  result.delays.resize(term_count);
  result.slews.resize(term_count);
  for (std::size_t term_idx = 0; term_idx < term_count; ++term_idx) {
    std::vector<double> weights(order, 0.0);
    const bool algebraic = term_idx < model.zeroCapacitanceTerms().size() && model.zeroCapacitanceTerms()[term_idx];
    for (int32_t index = 0; index < order; ++index) {
      double projection = 0.0;
      for (int32_t basis = 0; basis < order; ++basis) {
        projection += solver.eigenvectors()(basis, index) * model.get_projection_list()[basis][term_idx];
      }
      if (algebraic) {
        if (model.responseProjection().size() < static_cast<std::size_t>(order))
          return std::nullopt;
        projection = 0.;
        for (int32_t basis = 0; basis < order; ++basis) {
          if (model.responseProjection()[basis].size() <= term_idx)
            return std::nullopt;
          projection += solver.eigenvectors()(basis, index) * model.responseProjection()[basis][term_idx];
        }
        projection = time_constants[index] > 0. ? projection / time_constants[index] : 0.;
      }
      weights[index] = model.get_sqrt_total_capacitance() * solver.eigenvectors()(0, index) * projection;
    }
    auto terminal_constants = time_constants;
    if (algebraic) {
      terminal_constants.push_back(0.);
      weights.push_back(1. - std::accumulate(weights.begin(), weights.end(), 0.));
    }
    const auto& load = loads[term_idx];
    if (!(load.delay > 0.0 && load.delay < 1.0 && load.lower > 0.0 && load.upper < 1.0 && load.upper > load.lower && load.derate > 0.0)) {
      return std::nullopt;
    }
    double middle_time = inputPortCrossing(terminal_constants, weights, ramp, is_fall ? load.delay : 1.0 - load.delay);
    double early_time = inputPortCrossing(terminal_constants, weights, ramp, is_fall ? load.upper : 1.0 - load.lower);
    double late_time = inputPortCrossing(terminal_constants, weights, ramp, is_fall ? load.lower : 1.0 - load.upper);
    if (!std::isfinite(middle_time) || !std::isfinite(early_time) || !std::isfinite(late_time) || late_time < early_time) {
      return std::nullopt;
    }
    result.delays[term_idx] = term_idx == 0 ? 0.0 : middle_time - source_time;
    result.slews[term_idx] = term_idx == 0 ? input_slew : (late_time - early_time) / load.derate;
  }
  return result;
}

ParasiticArnoldiPoleResidue ArnoldiWaveformSolver::poleResidue(const ParasiticArnoldiModel& arnoldi_model, double drive_resistance)
{
  ParasiticArnoldiPoleResidue pole_residue;
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0) {
    return pole_residue;
  }

  std::vector<double> diagonal_list = arnoldi_model.get_diagonal_list();
  std::vector<double> off_diagonal_list = arnoldi_model.get_off_diagonal_list();
  diagonal_list.front() += drive_resistance * arnoldi_model.get_total_capacitance();

  std::vector<double> eigenvalue_list;
  std::vector<std::vector<double>> eigenvector_list;
  if (!solveTridiagonal(diagonal_list, off_diagonal_list, eigenvalue_list, eigenvector_list)) {
    return pole_residue;
  }

  int32_t order = arnoldi_model.get_order();
  std::size_t term_num = arnoldi_model.get_term_node_list().size();
  std::vector<double> pole_list(order, 0.0);
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    // Positive time constants have no absolute floor: clamping a fast pole
    // changes the current integral and can multiply the effective capacitance.
    pole_list[order_idx] = eigenvalue_list[order_idx] > 0. ? 1. / eigenvalue_list[order_idx] : std::numeric_limits<double>::infinity();
  }

  std::vector<std::vector<double>> residue_list(term_num, std::vector<double>(order, 0.0));
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    double source_projection = arnoldi_model.get_sqrt_total_capacitance() * eigenvector_list[order_idx][0];
    for (std::size_t term_idx = 0; term_idx < term_num; term_idx++) {
      double term_projection = 0.0;
      for (int32_t basis_idx = 0; basis_idx < order; basis_idx++) {
        term_projection += eigenvector_list[order_idx][basis_idx] * arnoldi_model.get_projection_list()[basis_idx][term_idx];
      }
      const auto& algebraic = arnoldi_model.zeroCapacitanceTerms();
      if (term_idx < algebraic.size() && algebraic[term_idx]) {
        // Reconstruct an algebraic node from A v = lambda v. Its voltage
        // cannot be recovered from the C-orthogonal projection alone.
        term_projection = drive_resistance * source_projection;
        for (int32_t basis_idx = 0; basis_idx < order; ++basis_idx)
          term_projection += eigenvector_list[order_idx][basis_idx] * arnoldi_model.responseProjection()[basis_idx][term_idx];
        term_projection = eigenvalue_list[order_idx] > 0. ? term_projection / eigenvalue_list[order_idx] : 0.;
      }
      residue_list[term_idx][order_idx] = source_projection * term_projection;
    }
  }

  const auto& algebraic = arnoldi_model.zeroCapacitanceTerms();
  if (std::find(algebraic.begin(), algebraic.end(), true) != algebraic.end()) {
    // An infinite pole represents instantaneous feedthrough exactly. Adding
    // fictitious capacitance would alter the physical load and its time scale.
    pole_list.push_back(std::numeric_limits<double>::infinity());
    for (size_t term = 0; term < term_num; ++term) {
      double direct = 0.;
      if (term < algebraic.size() && algebraic[term])
        direct = 1. - std::accumulate(residue_list[term].begin(), residue_list[term].end(), 0.);
      residue_list[term].push_back(direct);
    }
    ++order;
  }
  pole_residue.set_is_valid(true);
  pole_residue.set_order(order);
  pole_residue.set_pole_list(pole_list);
  pole_residue.set_residue_list(residue_list);
  return pole_residue;
}

bool ArnoldiWaveformSolver::solveTridiagonal(const std::vector<double>& diagonal_list, const std::vector<double>& off_diagonal_list,
                                             std::vector<double>& eigenvalue_list, std::vector<std::vector<double>>& eigenvector_list)
{
  const std::size_t order = diagonal_list.size();
  if (order == 0 || order > 32 || off_diagonal_list.size() + 1 < order) {
    return false;
  }
  Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(order, order);
  for (std::size_t i = 0; i < order; ++i) {
    matrix(i, i) = diagonal_list[i];
    if (i + 1 < order)
      matrix(i, i + 1) = matrix(i + 1, i) = off_diagonal_list[i];
  }
  if (!matrix.allFinite())
    return false;
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(matrix);
  if (solver.info() != Eigen::Success || !solver.eigenvalues().allFinite())
    return false;
  const double tolerance = 64. * std::numeric_limits<double>::epsilon() * solver.eigenvalues().cwiseAbs().maxCoeff();
  eigenvalue_list.resize(order);
  eigenvector_list.assign(order, std::vector<double>(order));
  for (std::size_t i = 0; i < order; ++i) {
    const auto column = order - 1 - i;
    const double value = solver.eigenvalues()[column];
    if (value < -tolerance)
      return false;
    eigenvalue_list[i] = std::max(value, 0.);
    for (std::size_t j = 0; j < order; ++j)
      eigenvector_list[i][j] = solver.eigenvectors()(j, column);
  }
  return true;
}

double ArnoldiWaveformSolver::hInverse(double value)
{
  if (!std::isfinite(value) || value < 0.)
    return std::numeric_limits<double>::quiet_NaN();
  if (value == 0.)
    return 0.;
  // h(x)=x+exp(-x)-1 is increasing for x>=0 and h^-1(y) lies in [y,y+1].
  // Near zero h(x)~x^2/2; this seed also avoids a very wide relative bracket.
  const double initial = value < 1. ? std::sqrt(value) * std::sqrt(2.) : value + 1.;
  double upper = value + 1.;
  // Addition can round the analytical upper bound below the root near a
  // binade boundary. Enclose the evaluated root before starting bisection.
  if (integratedDecay(upper) < value)
    upper = std::nextafter(upper, std::numeric_limits<double>::infinity());
  return bracketedRoot([value](double x) { return std::pair{integratedDecay(x) - value, -std::expm1(-x)}; }, value, upper, initial);
}

void ArnoldiWaveformSolver::solveRamp(double pole, double transition_time, double lower_threshold, double upper_threshold, double& ramp)
{
  const double initial = pole * ramp;
  ramp = std::numeric_limits<double>::quiet_NaN();
  if (!(pole > 0. && transition_time > 0. && lower_threshold > 0. && upper_threshold < 1. && lower_threshold < upper_threshold)
      || !std::isfinite(pole) || !std::isfinite(transition_time))
    return;
  const double target = pole * transition_time;
  if (!std::isfinite(target))
    return;
  // A step has the minimum span. If both crossings follow the ramp, this
  // span is independent of ramp width; choose the zero-width limit there.
  const double minimum_span = std::log(upper_threshold) - std::log(lower_threshold);
  const double tolerance = 8. * std::numeric_limits<double>::epsilon() * std::max(target, minimum_span);
  if (target < minimum_span - tolerance)
    return;
  if (target <= minimum_span + tolerance) {
    ramp = 0.;
    return;
  }
  const auto span = [&](double width) {
    double lower_time = 0., upper_time = 0., lower_derivative = 0., upper_derivative = 0.;
    rampPoint(width, lower_threshold, lower_time, lower_derivative);
    rampPoint(width, upper_threshold, upper_time, upper_derivative);
    return std::pair{lower_time - upper_time - target, lower_derivative - upper_derivative};
  };
  // An RC low-pass cannot slew faster than its ramp input, giving this
  // upper bound on the input width. Work in dimensionless pole*time units.
  double high = target / (upper_threshold - lower_threshold);
  double high_value = span(high).first;
  // Rounding can put the analytic bound just below the root. Establish
  // the sign change with evaluated values before starting the solve.
  while (std::isfinite(high_value) && high_value < 0.) {
    const double previous = high;
    high = high <= .5 * std::numeric_limits<double>::max() ? 2. * high : std::numeric_limits<double>::max();
    if (high == previous)
      return;
    high_value = span(high).first;
  }
  const double width = bracketedRoot(span, 0., high, initial);
  if (std::isfinite(width) && width >= 0. && std::isfinite(width / pole))
    ramp = width / pole;
}

double ArnoldiWaveformSolver::rampTime(double pole, double ramp, double voltage)
{
  if (!(pole > 0. && ramp >= 0.) || !std::isfinite(pole) || !std::isfinite(ramp))
    return std::numeric_limits<double>::quiet_NaN();
  double time = 0., derivative = 0.;
  rampPoint(pole * ramp, voltage, time, derivative);
  return time / pole;
}

void ArnoldiWaveformSolver::rampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative)
{
  pole_time = derivative = std::numeric_limits<double>::quiet_NaN();
  if (!(pole_ramp >= 0. && voltage > 0. && voltage < 1.) || !std::isfinite(pole_ramp))
    return;
  if (pole_ramp == 0.) {
    pole_time = -std::log(voltage);
    derivative = .5;
    return;
  }
  const double end_voltage = meanDecay(pole_ramp);
  if (voltage < end_voltage) {
    pole_time = pole_ramp + std::log(end_voltage) - std::log(voltage);
    // Bernoulli expansion of 1 + 1/(exp(u)-1) - 1/u avoids cancellation.
    if (pole_ramp < 1e-3) {
      const double squared = pole_ramp * pole_ramp;
      derivative = .5 + pole_ramp * (1. / 12. + squared * (-1. / 720. + squared / 30240.));
    } else {
      derivative = 1. + std::exp(-pole_ramp) / -std::expm1(-pole_ramp) - 1. / pole_ramp;
    }
    return;
  }
  pole_time = hInverse((1.0 - voltage) * pole_ramp);
  derivative = (1.0 - voltage) / -std::expm1(-pole_time);
}

double ArnoldiWaveformSolver::rampTimeSensitivity(double pole, double ramp, double voltage)
{
  if (!(pole > 0. && ramp >= 0. && voltage > 0. && voltage < 1.) || !std::isfinite(pole) || !std::isfinite(ramp))
    return std::numeric_limits<double>::quiet_NaN();
  const double width = pole * ramp;
  if (width == 0.)
    return -std::log(voltage);
  double time = 0., derivative = 0.;
  rampPoint(width, voltage, time, derivative);
  if (time <= width) {
    // 1 - x/(exp(x)-1), evaluated without cancellation or overflow.
    if (time < 1e-3) {
      const double squared = time * time;
      return .5 * time - squared * (1. / 12. + squared * (-1. / 720. + squared / 30240.));
    }
    return 1. - time * std::exp(-time) / -std::expm1(-time);
  }
  const double decay = meanDecay(width);
  return 1. + std::log(decay) - std::log(voltage) - width * std::exp(-width) / -std::expm1(-width);
}

std::optional<double> ArnoldiWaveformSolver::crossingCapacitance(double driver_ramp, double drive_resistance, double total_capacitance,
                                                                 double crossing_time, double voltage)
{
  if (!(driver_ramp > 0. && drive_resistance > 0. && total_capacitance > 0. && crossing_time > 0. && voltage > 0. && voltage < 1.)
      || !std::isfinite(driver_ramp) || !std::isfinite(drive_resistance) || !std::isfinite(total_capacitance) || !std::isfinite(crossing_time))
    return std::nullopt;
  const double ideal_time = driver_ramp * (1. - voltage);
  const double tolerance = 64. * std::numeric_limits<double>::epsilon() * std::max(crossing_time, driver_ramp);
  if (crossing_time < ideal_time - tolerance)
    return std::nullopt;
  const double maximum_crossing = rampTime(1. / (drive_resistance * total_capacitance), driver_ramp, voltage);
  // A distributed RC tail can cross late thresholds after the lumped total
  // load does. Its inverse then lies outside the permitted capacitance range;
  // clipping it would turn an infeasible equation into a false residual zero.
  if (!std::isfinite(maximum_crossing) || crossing_time > maximum_crossing + tolerance)
    return std::nullopt;
  if (std::abs(crossing_time - maximum_crossing) <= tolerance)
    return total_capacitance;
  double lower = 0., upper = total_capacitance;
  double capacitance = .5 * total_capacitance;
  for (int iteration = 0; iteration < 2 * std::numeric_limits<double>::digits; ++iteration) {
    const double tau = drive_resistance * capacitance;
    const double ramp_ratio = driver_ramp / tau;
    double normalized_time = 0., ramp_derivative = 0.;
    rampPoint(ramp_ratio, voltage, normalized_time, ramp_derivative);
    const double residual = tau * normalized_time - crossing_time;
    if (!std::isfinite(residual))
      return std::nullopt;
    if (std::abs(residual) <= tolerance)
      return capacitance;
    if (residual < 0.)
      lower = capacitance;
    else
      upper = capacitance;
    // t = R*C*f(ramp/(R*C)); retain the physical bracket if Newton leaves it.
    const double derivative = drive_resistance * rampTimeSensitivity(1. / tau, driver_ramp, voltage);
    const double candidate = derivative > 0. ? capacitance - residual / derivative : 0.;
    capacitance = std::isfinite(candidate) && candidate > lower && candidate < upper ? candidate : lower + .5 * (upper - lower);
  }
  return std::nullopt;
}

double ArnoldiWaveformSolver::effectiveCapacitance(double driver_ramp, double drive_resistance, const ParasiticArnoldiPoleResidue& pole_residue,
                                                   double effective_capacitance_time)
{
  if (!(drive_resistance > 0.0 && driver_ramp > 0.0 && effective_capacitance_time >= 0.0) || pole_residue.get_residue_list().empty()) {
    return 0.0;
  }
  const std::vector<double>& pole_list = pole_residue.get_pole_list();
  const std::vector<double>& residue_list = pole_residue.get_residue_list().front();
  double integrated_current = 0.0;
  double voltage_change = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    if (std::isinf(pole)) {
      // Feedthrough changes voltage without drawing stored charge.
      voltage_change += residue_list[pole_idx] * std::min(effective_capacitance_time / driver_ramp, 1.);
      continue;
    }
    double pole_ramp = pole * driver_ramp;
    double pole_time = pole * effective_capacitance_time;
    double integral = 0.0;
    if (effective_capacitance_time <= driver_ramp) {
      integral = integratedDecay(pole_time);
    } else {
      integral = integratedDecay(pole_ramp) + std::expm1(-pole_ramp) * std::expm1(-(pole_time - pole_ramp));
    }
    const double change = integral / pole_ramp;
    voltage_change += residue_list[pole_idx] * change;
    integrated_current += residue_list[pole_idx] * change / pole;
  }
  integrated_current /= drive_resistance;

  if (!(voltage_change > 0.0)) {
    return 0.0;
  }
  return integrated_current / voltage_change;
}

void ArnoldiWaveformSolver::crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double voltage,
                                         double& waveform_time)
{
  waveform_time = std::numeric_limits<double>::quiet_NaN();
  if (!(voltage > 0. && voltage < 1. && driver_ramp > 0.) || !std::isfinite(driver_ramp)
      || term_idx >= pole_residue.get_residue_list().size())
    return;
  const auto& poles = pole_residue.get_pole_list();
  const auto& residues = pole_residue.get_residue_list()[term_idx];
  if (poles.empty() || poles.size() != residues.size())
    return;
  for (std::size_t index = 0; index < poles.size(); ++index) {
    if (!(poles[index] > 0.) || !std::isfinite(residues[index]))
      return;
  }
  const auto response = [&](double time) {
    double value = 0., derivative = 0.;
    voltageDerivative(time, driver_ramp, poles, residues, value, derivative);
    return std::pair{value - voltage, derivative};
  };
  double low = 0., high = driver_ramp;
  const double initial_voltage = voltageAt(low, driver_ramp, poles, residues);
  if (!std::isfinite(initial_voltage) || initial_voltage < voltage)
    return;
  double high_voltage = voltageAt(high, driver_ramp, poles, residues);
  // Grow from the source ramp instead of using the slowest pole: an almost
  // weightless slow mode must not set the time resolution of every crossing.
  while (std::isfinite(high_voltage) && high_voltage > voltage) {
    low = high;
    high = high <= .5 * std::numeric_limits<double>::max() ? 2. * high : std::numeric_limits<double>::max();
    if (high == low)
      return;
    high_voltage = voltageAt(high, driver_ramp, poles, residues);
  }
  if (std::isfinite(high_voltage))
    waveform_time = bracketedRoot(response, low, high);
}

void ArnoldiWaveformSolver::crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double upper_threshold,
                                         double& upper_time, double mid_threshold, double& mid_time, double lower_threshold, double& lower_time)
{
  crossingTime(driver_ramp, pole_residue, term_idx, upper_threshold, upper_time);
  crossingTime(driver_ramp, pole_residue, term_idx, mid_threshold, mid_time);
  crossingTime(driver_ramp, pole_residue, term_idx, lower_threshold, lower_time);
}

double ArnoldiWaveformSolver::voltageAt(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list)
{
  double voltage = 0., derivative = 0.;
  voltageDerivative(time, driver_ramp, pole_list, residue_list, voltage, derivative);
  return voltage;
}

void ArnoldiWaveformSolver::voltageDerivative(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list,
                                              double& voltage, double& derivative)
{
  voltage = 0.0;
  derivative = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    if (std::isinf(pole)) {
      voltage += residue_list[pole_idx] * std::max(0., 1. - time / driver_ramp);
      if (time < driver_ramp)
        derivative -= residue_list[pole_idx] / driver_ramp;
      continue;
    }
    double pole_time = pole * time;
    double pole_ramp = pole * driver_ramp;
    double response = 0.0;
    double response_derivative = 0.0;
    if (time < driver_ramp) {
      const double fraction = time / driver_ramp;
      response = 1. - fraction + fraction * meanDecay(pole_time);
      response_derivative = std::expm1(-pole_time) / driver_ramp;
    } else {
      response = meanDecay(pole_ramp) * std::exp(-pole * (time - driver_ramp));
      response_derivative = -pole * response;
    }
    voltage += residue_list[pole_idx] * response;
    derivative += residue_list[pole_idx] * response_derivative;
  }
}

double ArnoldiWaveformSolver::inputPortCrossing(const std::vector<double>& time_constants, const std::vector<double>& weights, double ramp, double voltage)
{
  // Integrate each exponential step response over the source ramp. Remaining
  // voltage is the falling waveform, or one minus the rising waveform.
  const double failure = std::numeric_limits<double>::quiet_NaN();
  if (time_constants.empty() || time_constants.size() != weights.size() || !(ramp >= 0.) || !std::isfinite(ramp)
      || !(voltage > 0. && voltage < 1.))
    return failure;
  double fastest = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < time_constants.size(); ++index) {
    const double tau = time_constants[index];
    if (!(tau >= 0.) || !std::isfinite(tau) || !std::isfinite(weights[index]))
      return failure;
    if (tau > 0. && weights[index] != 0.)
      fastest = std::min(fastest, tau);
  }
  const auto evaluate = [&](double time) {
    double remaining = 0., derivative = 0.;
    for (std::size_t index = 0; index < time_constants.size(); ++index) {
      if (weights[index] == 0.)
        continue;
      const double tau = time_constants[index];
      double response = 0., slope = 0.;
      if (tau == 0.) {
        if (ramp > 0. && time < ramp) {
          response = 1. - time / ramp;
          slope = -1. / ramp;
        }
      } else if (ramp == 0.) {
        response = std::exp(-time / tau);
        slope = -response / tau;
      } else if (time < ramp) {
        const double fraction = time / ramp;
        response = 1. - fraction + fraction * meanDecay(time / tau);
        slope = std::expm1(-time / tau) / ramp;
      } else {
        response = meanDecay(ramp / tau) * std::exp(-(time - ramp) / tau);
        slope = -response / tau;
      }
      remaining += weights[index] * response;
      derivative += weights[index] * slope;
    }
    return std::pair{remaining - voltage, derivative};
  };
  const double initial = evaluate(0.).first;
  if (!std::isfinite(initial))
    return failure;
  if (initial <= 0.)
    return 0.;  // A step can cross immediately through algebraic feedthrough.
  double low = 0., high = ramp > 0. ? ramp : fastest;
  if (!std::isfinite(high))
    return failure;
  // An unobservable slow mode must not determine the crossing resolution.
  double value = evaluate(high).first;
  while (std::isfinite(value) && value > 0.) {
    low = high;
    high = high <= .5 * std::numeric_limits<double>::max() ? 2. * high : std::numeric_limits<double>::max();
    if (high == low)
      return failure;
    value = evaluate(high).first;
  }
  return std::isfinite(value) ? bracketedRoot(evaluate, low, high) : failure;
}

}  // namespace ista
