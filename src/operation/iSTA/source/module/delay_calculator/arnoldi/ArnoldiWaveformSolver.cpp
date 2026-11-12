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

#include "ThresholdNormalizer.hpp"
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

void ArnoldiWaveformSolver::thresholds(const TimingArc& timing_arc, TransType trans_type, double& slew_derate, double& lower_threshold, double& upper_threshold,
                                       double& voltage_log, double& min_slew_factor, double& x1, double& y1)
{
  slew_derate = timing_arc.get_slew_derate();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = normalizedThreshold(lower_threshold);
  upper_threshold = normalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  thresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void ArnoldiWaveformSolver::thresholds(const LibertyLibrary& timing_library, TransType trans_type, double& slew_derate, double& lower_threshold,
                                       double& upper_threshold, double& voltage_log, double& min_slew_factor, double& x1, double& y1)
{
  slew_derate = timing_library.get_slew_derate_from_library();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = normalizedThreshold(lower_threshold);
  upper_threshold = normalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  thresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void ArnoldiWaveformSolver::thresholdFactor(double lower_threshold, double upper_threshold, double& min_slew_factor, double& x1, double& y1)
{
  double upper_log = std::log(1.0 / upper_threshold);
  min_slew_factor = upper_log + hInverse((1.0 - upper_threshold) / upper_threshold - upper_log);
  double lower_log = std::log(1.0 / lower_threshold);
  double lower_factor = lower_log + hInverse((1.0 - lower_threshold) / lower_threshold - lower_log);
  double lower_ratio = (std::exp(lower_factor) - 1.0) / lower_factor;
  double denominator = std::log(lower_ratio / lower_threshold) - hInverse((1.0 - upper_threshold) * lower_factor);
  x1 = (upper_threshold - lower_threshold) / denominator;
  y1 = lower_factor * x1;
}

double ArnoldiWaveformSolver::hInverse(double value)
{
  double x = 0.0;
  if (value < 1.0) {
    x = std::sqrt(2.0 * value) + 0.4 * value;
    if (value < 1E-4) {
      return x;
    }
  } else {
    x = value + 1.0;
  }
  for (int32_t iter = 0; iter < 4; iter++) {
    double exp_x = std::exp(-x);
    double function_value = x + exp_x - 1.0 - value;
    x += function_value / (exp_x - 1.0);
  }
  return x;
}

void ArnoldiWaveformSolver::solveRamp(double pole, double transition_time, double lower_threshold, double upper_threshold, double& ramp)
{
  for (int32_t iter = 0; iter < 5; iter++) {
    double lower_pole_time = 0.0;
    double lower_derivative = 0.0;
    double upper_pole_time = 0.0;
    double upper_derivative = 0.0;
    rampPoint(pole * ramp, lower_threshold, lower_pole_time, lower_derivative);
    rampPoint(pole * ramp, upper_threshold, upper_pole_time, upper_derivative);
    double function_value = (lower_pole_time - upper_pole_time) / pole - transition_time;
    double derivative = lower_derivative - upper_derivative;
    if (std::abs(derivative) < ista::kTimingTolerance) {
      return;
    }
    ramp -= function_value / derivative;
    if (std::abs(function_value) < 1E-6) {
      return;
    }
  }
}

double ArnoldiWaveformSolver::rampTime(double pole, double ramp, double voltage)
{
  double pole_ramp = pole * ramp;
  if (pole_ramp > 30.0) {
    return (1.0 + pole_ramp * (1.0 - voltage)) / pole;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    return std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage)) / pole;
  }
  return hInverse((1.0 - voltage) * pole_ramp) / pole;
}

void ArnoldiWaveformSolver::rampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative)
{
  if (pole_ramp > 30.0) {
    pole_time = 1.0 + pole_ramp * (1.0 - voltage);
    derivative = 1.0 - voltage;
    return;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    pole_time = std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage));
    derivative = exp_pole_ramp / (exp_pole_ramp - 1.0) - 1.0 / pole_ramp;
    return;
  }
  pole_time = hInverse((1.0 - voltage) * pole_ramp);
  derivative = (1.0 - voltage) / (pole_time - (1.0 - voltage) * pole_ramp);
}

std::optional<double> ArnoldiWaveformSolver::crossingCapacitance(double driver_ramp, double drive_resistance, double total_capacitance, double crossing_time,
                                                                 double voltage)
{
  if (!(driver_ramp > 0. && drive_resistance > 0. && total_capacitance > 0. && crossing_time > 0. && voltage > 0. && voltage < 1.)
      || !std::isfinite(driver_ramp) || !std::isfinite(drive_resistance) || !std::isfinite(total_capacitance) || !std::isfinite(crossing_time))
    return std::nullopt;
  const double ideal_time = driver_ramp * (1. - voltage);
  const double tolerance = 1e-10 * std::max(crossing_time, 1e-6);
  if (crossing_time < ideal_time - tolerance)
    return std::nullopt;
  if (crossing_time >= rampTime(1. / (drive_resistance * total_capacitance), driver_ramp, voltage))
    return total_capacitance;
  double lower = 0., upper = total_capacitance;
  double capacitance = std::clamp((crossing_time - ideal_time) / drive_resistance, total_capacitance * 1e-12, total_capacitance);
  for (int iteration = 0; iteration < 40; ++iteration) {
    const double tau = drive_resistance * capacitance;
    const double ramp_ratio = driver_ramp / tau;
    double normalized_time = 0., ramp_derivative = 0.;
    rampPoint(ramp_ratio, voltage, normalized_time, ramp_derivative);
    const double residual = tau * normalized_time - crossing_time;
    if (std::abs(residual) <= tolerance)
      return capacitance;
    if (residual < 0.)
      lower = capacitance;
    else
      upper = capacitance;
    // t = R*C*f(ramp/(R*C)); differentiate with respect to capacitance.
    const double derivative = drive_resistance * (normalized_time - ramp_ratio * ramp_derivative);
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
  if (!(voltage > 0. && voltage < 1. && driver_ramp > 0.) || term_idx >= pole_residue.get_residue_list().size())
    return;
  const auto& poles = pole_residue.get_pole_list();
  const auto& residues = pole_residue.get_residue_list()[term_idx];
  double longest = 0.;
  for (double pole : poles) {
    if (!(pole > 0.))
      return;
    if (std::isfinite(pole))
      longest = std::max(longest, 1. / pole);
  }
  double low = 0., high = driver_ramp + 64. * longest;
  if (!std::isfinite(high) || !(voltageAt(low, driver_ramp, poles, residues) >= voltage) || !(voltageAt(high, driver_ramp, poles, residues) <= voltage))
    return;
  // Keep a genuine voltage bracket for arbitrary receiver thresholds. The old
  // midpoint-oriented starting guesses could invert it for 10/90% crossings.
  for (int iteration = 0; iteration < 80; ++iteration) {
    const double mid = low + .5 * (high - low);
    const double value = voltageAt(mid, driver_ramp, poles, residues);
    if (!std::isfinite(value))
      return;
    if (value > voltage)
      low = mid;
    else
      high = mid;
  }
  waveform_time = low + .5 * (high - low);
}

void ArnoldiWaveformSolver::crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double upper_threshold,
                                         double& upper_time, double mid_threshold, double& mid_time, double lower_threshold, double& lower_time)
{
  const std::vector<double>& source_pole_list = pole_residue.get_pole_list();
  const std::vector<double>& source_residue_list = pole_residue.get_residue_list()[term_idx];
  int32_t order = pole_residue.get_order();
  while (order > 1 && source_residue_list[order - 1] < 1E-8 && source_residue_list[order - 1] > -1E-8) {
    order--;
  }
  if (order <= 0) {
    upper_time = 0.0;
    mid_time = 0.0;
    lower_time = 0.0;
    return;
  }

  std::vector<double> pole_list;
  std::vector<double> residue_list;
  for (int32_t idx = 0; idx < order; idx++) {
    pole_list.push_back(source_pole_list[idx]);
    residue_list.push_back(source_residue_list[idx]);
  }

  int32_t pole_idx = 0;
  if (residue_list[0] < 0.5) {
    for (int32_t idx = 1; idx < order; idx++) {
      if (residue_list[idx] > 0.3 && residue_list[idx] > residue_list[0]) {
        pole_idx = idx;
        break;
      }
    }
  }
  double search_pole = std::min(pole_list[pole_idx], 10.0);
  double waveform_at_ramp = 0.0;
  for (int32_t idx = 0; idx < order; idx++) {
    double pole_ramp = pole_list[idx] * driver_ramp;
    waveform_at_ramp += residue_list[idx] * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
  }

  double hi_min_time = 0.0;
  double hi_max_time = 0.0;
  double hi_min_voltage = 0.0;
  double hi_max_voltage = 0.0;
  double mid_min_time = 0.0;
  double mid_max_time = 0.0;
  double mid_min_voltage = 0.0;
  double mid_max_voltage = 0.0;
  double lo_min_time = 0.0;
  double lo_max_time = 0.0;
  double lo_min_voltage = 0.0;
  double lo_max_voltage = 0.0;

  if (waveform_at_ramp < lower_threshold) {
    lo_max_time = driver_ramp;
    lo_max_voltage = waveform_at_ramp;
    double candidate_time = upper_threshold * driver_ramp;
    double candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    if (candidate_voltage < mid_threshold) {
      hi_max_time = candidate_time;
      mid_max_time = candidate_time;
      lo_min_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      mid_max_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time = mid_threshold * driver_ramp;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
      if (candidate_voltage > upper_threshold) {
        hi_min_time = candidate_time;
        mid_min_time = candidate_time;
        lo_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
        mid_min_voltage = candidate_voltage;
        lo_min_voltage = candidate_voltage;
        if (candidate_voltage < mid_threshold) {
          mid_max_time = candidate_time;
          mid_max_voltage = candidate_voltage;
        } else {
          mid_min_time = candidate_time;
          mid_min_voltage = candidate_voltage;
        }
      } else {
        hi_max_time = candidate_time;
        mid_min_time = candidate_time;
        hi_max_voltage = candidate_voltage;
        mid_min_voltage = candidate_voltage;
        candidate_time = lower_threshold * driver_ramp;
        candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
        hi_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
      }
    } else {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      mid_max_time = lo_max_time;
      mid_max_voltage = lo_max_voltage;
      if (candidate_voltage > upper_threshold) {
        hi_min_time = mid_min_time;
        hi_min_voltage = mid_min_voltage;
        hi_max_time = mid_max_time;
        hi_max_voltage = mid_max_voltage;
      } else {
        hi_max_time = mid_min_time;
        hi_max_voltage = mid_min_voltage;
        candidate_time = lower_threshold * driver_ramp;
        candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
        hi_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
      }
    }
  } else if (waveform_at_ramp < mid_threshold) {
    hi_max_time = driver_ramp;
    mid_max_time = driver_ramp;
    lo_min_time = driver_ramp;
    hi_max_voltage = waveform_at_ramp;
    mid_max_voltage = waveform_at_ramp;
    lo_min_voltage = waveform_at_ramp;
    double candidate_time = driver_ramp + 1.6 / search_pole;
    double candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
    candidate_time = mid_threshold * driver_ramp;
    candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    mid_min_time = candidate_time;
    mid_min_voltage = candidate_voltage;
    if (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    } else {
      hi_max_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      candidate_time = lower_threshold * driver_ramp;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    }
  } else if (waveform_at_ramp < upper_threshold) {
    hi_max_time = driver_ramp;
    mid_min_time = driver_ramp;
    lo_min_time = driver_ramp;
    hi_max_voltage = waveform_at_ramp;
    mid_min_voltage = waveform_at_ramp;
    lo_min_voltage = waveform_at_ramp;
    double candidate_time = lower_threshold * driver_ramp;
    double candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    hi_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    candidate_time = driver_ramp + 0.7 / search_pole;
    candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 0.7 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    if (candidate_voltage < lower_threshold) {
      lo_max_time = candidate_time;
      lo_max_voltage = candidate_voltage;
    } else {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
      while (candidate_voltage > lower_threshold) {
        lo_min_time = candidate_time;
        lo_min_voltage = candidate_voltage;
        candidate_time += 1.0 / search_pole;
        candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
      }
      lo_max_time = candidate_time;
      lo_max_voltage = candidate_voltage;
    }
  } else {
    double candidate_time = driver_ramp;
    double candidate_voltage = waveform_at_ramp;
    hi_min_time = candidate_time;
    mid_min_time = candidate_time;
    lo_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    mid_min_voltage = candidate_voltage;
    lo_min_voltage = candidate_voltage;
    while (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    hi_max_time = candidate_time;
    hi_max_voltage = candidate_voltage;
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = voltageAt(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
  }

  upper_time = bracketedTime(driver_ramp, pole_list, residue_list, upper_threshold, hi_min_time, hi_max_time, hi_min_voltage, hi_max_voltage);
  mid_time = bracketedTime(driver_ramp, pole_list, residue_list, mid_threshold, mid_min_time, mid_max_time, mid_min_voltage, mid_max_voltage);
  lower_time = bracketedTime(driver_ramp, pole_list, residue_list, lower_threshold, lo_min_time, lo_max_time, lo_min_voltage, lo_max_voltage);
}

double ArnoldiWaveformSolver::voltageAt(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list)
{
  double voltage = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    if (std::isinf(pole_list[pole_idx])) {
      voltage += residue_list[pole_idx] * std::max(0., 1. - time / driver_ramp);
      continue;
    }
    double pole_time = pole_list[pole_idx] * time;
    double pole_ramp = pole_list[pole_idx] * driver_ramp;
    double response = 0.0;
    if (time < driver_ramp) {
      response = 1.0 - time / driver_ramp - std::expm1(-pole_time) / pole_ramp;
    } else {
      response = -std::exp(pole_ramp - pole_time) * std::expm1(-pole_ramp) / pole_ramp;
    }
    voltage += residue_list[pole_idx] * response;
  }
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
      double ramp_response = -std::expm1(-pole_time) / pole_ramp;
      response = 1.0 - time / driver_ramp + ramp_response;
      response_derivative = -pole * ramp_response;
    } else {
      response = -std::exp(pole_ramp - pole_time) * std::expm1(-pole_ramp) / pole_ramp;
      response_derivative = -pole * response;
    }
    voltage += residue_list[pole_idx] * response;
    derivative += residue_list[pole_idx] * response_derivative;
  }
}

double ArnoldiWaveformSolver::bracketedTime(double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list, double voltage,
                                            double lower_time, double upper_time, double lower_voltage, double upper_voltage)
{
  double lower_function = lower_voltage - voltage;
  double upper_function = upper_voltage - voltage;
  if (lower_function == 0.0) {
    return lower_time;
  }
  if (upper_function == 0.0) {
    return upper_time;
  }

  double result_time = (lower_function * upper_time - upper_function * lower_time) / (lower_function - upper_function);
  double low_time = lower_time;
  double high_time = upper_time;
  if (lower_function < upper_function) {
    low_time = lower_time;
    high_time = upper_time;
    if (0.0 < lower_function) {
      return lower_time;
    }
    if (upper_function < 0.0) {
      return upper_time;
    }
  } else {
    low_time = upper_time;
    high_time = lower_time;
    if (0.0 < upper_function) {
      return upper_time;
    }
    if (lower_function < 0.0) {
      return lower_time;
    }
  }

  double old_delta = std::abs(upper_time - lower_time);
  double delta = old_delta;
  double function_value = 0.0;
  double derivative = 0.0;
  voltageDerivative(result_time, driver_ramp, pole_list, residue_list, function_value, derivative);
  function_value -= voltage;
  double last_function_value = 0.0;
  for (int32_t iter = 1; iter < 10; iter++) {
    if ((((result_time - high_time) * derivative - function_value) * ((result_time - low_time) * derivative - function_value) >= 0.0)
        || (std::abs(2.0 * function_value) > std::abs(old_delta * derivative))) {
      old_delta = delta;
      delta = 0.5 * (high_time - low_time);
      if (last_function_value * function_value > 0.0) {
        if (function_value < 0.0) {
          delta = 0.9348 * (high_time - low_time);
        } else {
          delta = 0.0625 * (high_time - low_time);
        }
      }
      last_function_value = function_value;
      result_time = low_time + delta;
      if (low_time == result_time) {
        return result_time;
      }
    } else {
      old_delta = delta;
      delta = function_value / derivative;
      last_function_value = 0.0;
      double temp_time = result_time;
      result_time -= delta;
      if (temp_time == result_time) {
        return result_time;
      }
    }
    if (std::abs(delta) < 1E-6) {
      return result_time;
    }
    voltageDerivative(result_time, driver_ramp, pole_list, residue_list, function_value, derivative);
    function_value -= voltage;
    if (function_value < 0.0) {
      low_time = result_time;
    } else {
      high_time = result_time;
    }
  }
  if (std::abs(function_value) < 1E-6) {
    return result_time;
  }
  return 0.5 * (low_time + high_time);
}

double ArnoldiWaveformSolver::inputPortCrossing(const std::vector<double>& time_constants, const std::vector<double>& weights, double ramp, double voltage)
{
  // Integrate each exponential step response over the source ramp. Remaining
  // voltage is the falling waveform, or one minus the rising waveform.
  double low = 0.0;
  double high = ramp + 64.0 * *std::max_element(time_constants.begin(), time_constants.end());
  for (int32_t iteration = 0; iteration < 64; ++iteration) {
    double time = 0.5 * (low + high);
    double remaining = 0.0;
    for (std::size_t index = 0; index < time_constants.size(); ++index) {
      double tau = time_constants[index];
      double response = 0.0;
      if (tau == 0.0) {
        response = ramp > 0.0 ? std::max(0.0, 1.0 - time / ramp) : 0.0;
      } else if (ramp == 0.0) {
        response = std::exp(-time / tau);
      } else if (time < ramp) {
        response = 1.0 - time / ramp - tau / ramp * std::expm1(-time / tau);
      } else {
        response = -tau / ramp * std::expm1(-ramp / tau) * std::exp(-(time - ramp) / tau);
      }
      remaining += weights[index] * response;
    }
    if (remaining > voltage) {
      low = time;
    } else {
      high = time;
    }
  }
  return 0.5 * (low + high);
}

}  // namespace ista
