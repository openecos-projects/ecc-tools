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
/**
 * @file FastSTADmpCeffEquations.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief DMP effective-capacitance equation solver for CTS fast STA.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>

#include "FastSTADmpCeffSolver.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts::fast_sta_dmp {

auto DriverRampSolver::capacitiveRampResponse(double t_ns, double ramp_start_ns, double ramp_duration_ns, double load_cap_pf) const -> std::pair<double, double>
{
  const auto t1_ns = t_ns - ramp_start_ns;
  if (t1_ns <= 0.0) {
    return {0.0, t1_ns};
  }
  if (ramp_duration_ns <= kEpsilon) {
    return {0.0, t1_ns};
  }
  if (t1_ns <= ramp_duration_ns) {
    return {capacitiveRampIntegral(t1_ns, load_cap_pf) / ramp_duration_ns, t1_ns};
  }
  return {(capacitiveRampIntegral(t1_ns, load_cap_pf) - capacitiveRampIntegral(t1_ns - ramp_duration_ns, load_cap_pf)) / ramp_duration_ns, t1_ns};
}

auto DriverRampSolver::capacitiveRampIntegral(double t_ns, double load_cap_pf) const -> double
{
  const auto tau_ns = _rd_ns_per_pf * load_cap_pf;
  if (tau_ns <= kEpsilon) {
    return t_ns;
  }
  return t_ns - tau_ns * (1.0 - DmpExp(-t_ns / tau_ns));
}

auto DriverRampSolver::capacitiveRampSlope(double t_ns, double load_cap_pf) const -> double
{
  const auto tau_ns = _rd_ns_per_pf * load_cap_pf;
  if (tau_ns <= kEpsilon) {
    return 1.0;
  }
  return 1.0 - DmpExp(-t_ns / tau_ns);
}

auto DriverRampSolver::capacitiveRampCapDerivative(double t_ns, double load_cap_pf) const -> double
{
  const auto tau_ns = _rd_ns_per_pf * load_cap_pf;
  if (tau_ns <= kEpsilon || load_cap_pf <= kEpsilon) {
    return 0.0;
  }
  return _rd_ns_per_pf * ((1.0 + t_ns / tau_ns) * DmpExp(-t_ns / tau_ns) - 1.0);
}

auto DriverRampSolver::capacitiveRampDerivatives(double t_ns, double ramp_start_ns, double ramp_duration_ns, double load_cap_pf) const
    -> std::tuple<double, double, double>
{
  const auto t1_ns = t_ns - ramp_start_ns;
  if (t1_ns <= 0.0 || ramp_duration_ns <= kEpsilon) {
    return {0.0, 0.0, 0.0};
  }
  if (t1_ns <= ramp_duration_ns) {
    return {-capacitiveRampSlope(t1_ns, load_cap_pf) / ramp_duration_ns, -capacitiveRampIntegral(t1_ns, load_cap_pf) / (ramp_duration_ns * ramp_duration_ns),
            capacitiveRampCapDerivative(t1_ns, load_cap_pf) / ramp_duration_ns};
  }
  return {-(capacitiveRampSlope(t1_ns, load_cap_pf) - capacitiveRampSlope(t1_ns - ramp_duration_ns, load_cap_pf)) / ramp_duration_ns,
          -(capacitiveRampIntegral(t1_ns, load_cap_pf) + capacitiveRampIntegral(t1_ns - ramp_duration_ns, load_cap_pf)) / (ramp_duration_ns * ramp_duration_ns)
              + capacitiveRampSlope(t1_ns - ramp_duration_ns, load_cap_pf) / ramp_duration_ns,
          (capacitiveRampCapDerivative(t1_ns, load_cap_pf) - capacitiveRampCapDerivative(t1_ns - ramp_duration_ns, load_cap_pf)) / ramp_duration_ns};
}

auto DriverRampSolver::evaluateResiduals() -> bool
{
  if (_algorithm == FastStaDmpAlgorithm::kPi) {
    return evaluatePiResiduals();
  }
  if (_algorithm == FastStaDmpAlgorithm::kZeroNearCap) {
    return evaluateSinglePoleResiduals();
  }
  return false;
}

auto DriverRampSolver::evaluatePiResiduals() -> bool
{
  const auto ramp_start_ns = _variables.at(ToIndex(DriverVariable::kRampStart));
  const auto ramp_duration_ns = _variables.at(ToIndex(DriverVariable::kRampDuration));
  const auto ceff_pf = _variables.at(ToIndex(DriverVariable::kEffectiveCapacitance));
  if (ceff_pf < 0.0 || ceff_pf > _far_cap_pf + _near_cap_pf || ramp_duration_ns <= 0.0) {
    return false;
  }

  const auto gate_values = evaluateGateThresholds(ceff_pf);
  if (!gate_values.valid) {
    return false;
  }
  auto ceff_time_ns = gate_values.measured_slew_ns / (_slew_upper_threshold - _slew_lower_threshold);
  ceff_time_ns = std::min(ceff_time_ns, 1.4 * ramp_duration_ns);
  const auto exp_p1_dt = DmpExp(-_first_pole_per_ns * ramp_duration_ns);
  const auto exp_p2_dt = DmpExp(-_second_pole_per_ns * ramp_duration_ns);
  const auto tau_ns = _rd_ns_per_pf * ceff_pf;
  if (tau_ns <= kEpsilon) {
    return false;
  }
  const auto exp_dt_rd_ceff = DmpExp(-ramp_duration_ns / tau_ns);
  const auto voltage_at_delay_threshold = capacitiveRampResponse(gate_values.delay_ns, ramp_start_ns, ramp_duration_ns, ceff_pf).first;
  const auto voltage_at_slew_lower_threshold = capacitiveRampResponse(gate_values.lower_threshold_time_ns, ramp_start_ns, ramp_duration_ns, ceff_pf).first;
  _residuals.at(ToIndex(DriverEquation::kAverageCurrentBalance)) = averageCurrentResidual(ramp_duration_ns, ceff_time_ns, ceff_pf);
  _residuals.at(ToIndex(DriverEquation::kDelayCrossing)) = voltage_at_delay_threshold - _output_threshold;
  _residuals.at(ToIndex(DriverEquation::kSlewLowerCrossing)) = voltage_at_slew_lower_threshold - _slew_lower_threshold;
  _jacobian.at(ToIndex(DriverEquation::kAverageCurrentBalance)).at(ToIndex(DriverVariable::kRampStart)) = 0.0;
  _jacobian.at(ToIndex(DriverEquation::kAverageCurrentBalance)).at(ToIndex(DriverVariable::kRampDuration))
      = (-_steady_current_coefficient * ramp_duration_ns + _first_pole_current_coefficient * ramp_duration_ns * exp_p1_dt
         - (2.0 * _first_pole_current_coefficient / _first_pole_per_ns) * (1.0 - exp_p1_dt) + _second_pole_current_coefficient * ramp_duration_ns * exp_p2_dt
         - (2.0 * _second_pole_current_coefficient / _second_pole_per_ns) * (1.0 - exp_p2_dt)
         + _rd_ns_per_pf * ceff_pf * (ramp_duration_ns + ramp_duration_ns * exp_dt_rd_ceff - 2.0 * _rd_ns_per_pf * ceff_pf * (1.0 - exp_dt_rd_ceff)))
        / (_rd_ns_per_pf * ramp_duration_ns * ramp_duration_ns * ramp_duration_ns);
  _jacobian.at(ToIndex(DriverEquation::kAverageCurrentBalance)).at(ToIndex(DriverVariable::kEffectiveCapacitance))
      = (2.0 * _rd_ns_per_pf * ceff_pf - ramp_duration_ns - (2.0 * _rd_ns_per_pf * ceff_pf + ramp_duration_ns) * exp_dt_rd_ceff)
        / (ramp_duration_ns * ramp_duration_ns);

  std::tie(_jacobian.at(ToIndex(DriverEquation::kSlewLowerCrossing)).at(ToIndex(DriverVariable::kRampStart)),
           _jacobian.at(ToIndex(DriverEquation::kSlewLowerCrossing)).at(ToIndex(DriverVariable::kRampDuration)),
           _jacobian.at(ToIndex(DriverEquation::kSlewLowerCrossing)).at(ToIndex(DriverVariable::kEffectiveCapacitance)))
      = capacitiveRampDerivatives(gate_values.lower_threshold_time_ns, ramp_start_ns, ramp_duration_ns, ceff_pf);
  std::tie(_jacobian.at(ToIndex(DriverEquation::kDelayCrossing)).at(ToIndex(DriverVariable::kRampStart)),
           _jacobian.at(ToIndex(DriverEquation::kDelayCrossing)).at(ToIndex(DriverVariable::kRampDuration)),
           _jacobian.at(ToIndex(DriverEquation::kDelayCrossing)).at(ToIndex(DriverVariable::kEffectiveCapacitance)))
      = capacitiveRampDerivatives(gate_values.delay_ns, ramp_start_ns, ramp_duration_ns, ceff_pf);
  return allFinite(_residuals) && allFinite(_jacobian);
}

auto DriverRampSolver::evaluateSinglePoleResiduals() -> bool
{
  auto ramp_start_ns = _variables.at(ToIndex(DriverVariable::kRampStart));
  auto ramp_duration_ns = _variables.at(ToIndex(DriverVariable::kRampDuration));
  const auto gate_values = evaluateGateThresholds(_effective_capacitance_pf);
  if (!gate_values.valid) {
    return false;
  }
  if (ramp_duration_ns <= 0.0) {
    ramp_duration_ns = std::max(std::abs(gate_values.delay_ns - gate_values.lower_threshold_time_ns) / 100.0, 1e-6);
    _variables.at(ToIndex(DriverVariable::kRampDuration)) = ramp_duration_ns;
  }
  _residuals.at(ToIndex(DriverEquation::kDelayCrossing))
      = capacitiveRampResponse(gate_values.delay_ns, ramp_start_ns, ramp_duration_ns, _effective_capacitance_pf).first - _output_threshold;
  _residuals.at(ToIndex(DriverEquation::kSlewLowerCrossing))
      = capacitiveRampResponse(gate_values.lower_threshold_time_ns, ramp_start_ns, ramp_duration_ns, _effective_capacitance_pf).first - _slew_lower_threshold;

  double unused = 0.0;
  std::tie(_jacobian.at(ToIndex(DriverEquation::kSlewLowerCrossing)).at(ToIndex(DriverVariable::kRampStart)),
           _jacobian.at(ToIndex(DriverEquation::kSlewLowerCrossing)).at(ToIndex(DriverVariable::kRampDuration)), unused)
      = capacitiveRampDerivatives(gate_values.lower_threshold_time_ns, ramp_start_ns, ramp_duration_ns, _effective_capacitance_pf);
  std::tie(_jacobian.at(ToIndex(DriverEquation::kDelayCrossing)).at(ToIndex(DriverVariable::kRampStart)),
           _jacobian.at(ToIndex(DriverEquation::kDelayCrossing)).at(ToIndex(DriverVariable::kRampDuration)), unused)
      = capacitiveRampDerivatives(gate_values.delay_ns, ramp_start_ns, ramp_duration_ns, _effective_capacitance_pf);
  return allFinite(_residuals) && allFinite(_jacobian);
}

auto DriverRampSolver::averageCurrentResidual(double ramp_duration_ns, double ceff_time_ns, double ceff_pf) const -> double
{
  const auto exp_p1 = DmpExp(-_first_pole_per_ns * ceff_time_ns);
  const auto exp_p2 = DmpExp(-_second_pole_per_ns * ceff_time_ns);
  const auto tau_ns = _rd_ns_per_pf * ceff_pf;
  if (tau_ns <= kEpsilon || ceff_time_ns <= kEpsilon || ramp_duration_ns <= kEpsilon) {
    return 0.0;
  }
  const auto exp_ceff = DmpExp(-ceff_time_ns / tau_ns);
  const auto ipi = (_steady_current_coefficient * ceff_time_ns + (_first_pole_current_coefficient / _first_pole_per_ns) * (1.0 - exp_p1)
                    + (_second_pole_current_coefficient / _second_pole_per_ns) * (1.0 - exp_p2))
                   / (_rd_ns_per_pf * ceff_time_ns * ramp_duration_ns);
  const auto iceff = (tau_ns * ceff_time_ns - tau_ns * tau_ns * (1.0 - exp_ceff)) / (_rd_ns_per_pf * ceff_time_ns * ramp_duration_ns);
  return ipi - iceff;
}

auto DriverRampSolver::solveNonlinearSystem() -> bool
{
  for (std::size_t iter = 0U; iter < kNewtonRaphsonMaxIter; ++iter) {
    if (!evaluateResiduals()) {
      return false;
    }
    for (std::size_t i = 0U; i < _active_variable_count; ++i) {
      _newton_step.at(i) = -_residuals.at(i);
    }
    if (!factorJacobian() || !solveNewtonStep()) {
      return false;
    }

    auto all_under_tolerance = true;
    for (std::size_t i = 0U; i < _active_variable_count; ++i) {
      if (std::abs(_newton_step.at(i)) > std::max(std::abs(_variables.at(i)), 1e-12) * kDriverParamTolerance) {
        all_under_tolerance = false;
      }
      _variables.at(i) += _newton_step.at(i);
    }
    if (all_under_tolerance) {
      return evaluateResiduals();
    }
  }
  return false;
}

auto DriverRampSolver::factorJacobian() -> bool
{
  for (std::size_t i = 0U; i < _active_variable_count; ++i) {
    auto big = 0.0;
    for (std::size_t j = 0U; j < _active_variable_count; ++j) {
      big = std::max(big, std::abs(_jacobian.at(i).at(j)));
    }
    if (big <= kEpsilon) {
      return false;
    }
    _row_scales.at(i) = 1.0 / big;
  }

  for (std::size_t j = 0U; j < _active_variable_count; ++j) {
    for (std::size_t i = 0U; i < j; ++i) {
      auto sum = _jacobian.at(i).at(j);
      for (std::size_t k = 0U; k < i; ++k) {
        sum -= _jacobian.at(i).at(k) * _jacobian.at(k).at(j);
      }
      _jacobian.at(i).at(j) = sum;
    }

    auto big = 0.0;
    auto imax = j;
    for (std::size_t i = j; i < _active_variable_count; ++i) {
      auto sum = _jacobian.at(i).at(j);
      for (std::size_t k = 0U; k < j; ++k) {
        sum -= _jacobian.at(i).at(k) * _jacobian.at(k).at(j);
      }
      _jacobian.at(i).at(j) = sum;
      const auto scaled = _row_scales.at(i) * std::abs(sum);
      if (scaled >= big) {
        big = scaled;
        imax = i;
      }
    }
    if (j != imax) {
      for (std::size_t k = 0U; k < _active_variable_count; ++k) {
        std::swap(_jacobian.at(imax).at(k), _jacobian.at(j).at(k));
      }
      _row_scales.at(imax) = _row_scales.at(j);
    }
    _pivot_rows.at(j) = imax;
    if (std::abs(_jacobian.at(j).at(j)) <= kEpsilon) {
      _jacobian.at(j).at(j) = kEpsilon;
    }
    if (j != _active_variable_count - 1) {
      const auto pivot = 1.0 / _jacobian.at(j).at(j);
      for (std::size_t i = j + 1U; i < _active_variable_count; ++i) {
        _jacobian.at(i).at(j) *= pivot;
      }
    }
  }
  return true;
}

auto DriverRampSolver::solveNewtonStep() -> bool
{
  auto non_zero = kMaxOrder;
  for (std::size_t i = 0U; i < _active_variable_count; ++i) {
    const auto iperm = _pivot_rows.at(i);
    if (iperm >= _active_variable_count) {
      return false;
    }
    auto sum = _newton_step.at(iperm);
    _newton_step.at(iperm) = _newton_step.at(i);
    if (non_zero != kMaxOrder) {
      for (std::size_t j = non_zero; j < i; ++j) {
        sum -= _jacobian.at(i).at(j) * _newton_step.at(j);
      }
    } else if (sum != 0.0) {
      non_zero = i;
    }
    _newton_step.at(i) = sum;
  }
  for (std::size_t i = _active_variable_count; i-- > 0U;) {
    auto sum = _newton_step.at(i);
    for (std::size_t j = i + 1U; j < _active_variable_count; ++j) {
      sum -= _jacobian.at(i).at(j) * _newton_step.at(j);
    }
    if (std::abs(_jacobian.at(i).at(i)) <= kEpsilon) {
      return false;
    }
    _newton_step.at(i) = sum / _jacobian.at(i).at(i);
  }
  return allFinite(_newton_step);
}

auto DriverRampSolver::measureDriverThresholds() -> std::optional<std::pair<double, double>>
{
  const auto upper = driverThresholdUpperBound();
  const auto delay = findDriverThresholdTime(_output_threshold, _ramp_start_ns, upper);
  if (!delay.has_value()) {
    return std::nullopt;
  }
  const auto tl = findDriverThresholdTime(_slew_lower_threshold, _ramp_start_ns, *delay);
  const auto th = findDriverThresholdTime(_slew_upper_threshold, *delay, upper);
  if (!tl.has_value() || !th.has_value()) {
    return std::nullopt;
  }
  return std::pair<double, double>{*delay, std::max(0.0, (*th - *tl) / _slew_derate)};
}

auto DriverRampSolver::findDriverThresholdTime(double threshold, double lower, double upper) -> std::optional<double>
{
  if (upper <= lower) {
    upper = lower + std::max(_ramp_duration_ns, 1e-6);
  }
  return FindRoot(
      [&](double t, double& y_value, double& dy_value) -> void {
        const auto [vo_value, dvo_dt] = driverRampResponse(t);
        y_value = vo_value - threshold;
        dy_value = dvo_dt;
      },
      lower, upper, kThresholdTimeTolerance, kFindRootMaxIter);
}

auto DriverRampSolver::driverRampResponse(double t_ns) const -> std::pair<double, double>
{
  const auto t1_ns = t_ns - _ramp_start_ns;
  if (t1_ns <= 0.0 || _ramp_duration_ns <= kEpsilon) {
    return {0.0, 0.0};
  }
  if (t1_ns <= _ramp_duration_ns) {
    const auto [value, deriv] = driverRampIntegral(t1_ns);
    return {value / _ramp_duration_ns, deriv / _ramp_duration_ns};
  }
  const auto [value, deriv] = driverRampIntegral(t1_ns);
  const auto [dt_value, dt_deriv] = driverRampIntegral(t1_ns - _ramp_duration_ns);
  return {(value - dt_value) / _ramp_duration_ns, (deriv - dt_deriv) / _ramp_duration_ns};
}

auto DriverRampSolver::driverRampIntegral(double t_ns) const -> std::pair<double, double>
{
  if (_algorithm == FastStaDmpAlgorithm::kPi) {
    const auto exp_p1 = DmpExp(-_first_pole_per_ns * t_ns);
    const auto exp_p2 = DmpExp(-_second_pole_per_ns * t_ns);
    return {_waveform_scale * (_waveform_offset + _waveform_slope * t_ns + _first_pole_weight * exp_p1 + _second_pole_weight * exp_p2),
            _waveform_scale * (_waveform_slope - _first_pole_weight * _first_pole_per_ns * exp_p1 - _second_pole_weight * _second_pole_per_ns * exp_p2)};
  }
  if (_algorithm == FastStaDmpAlgorithm::kZeroNearCap) {
    const auto exp_p1 = DmpExp(-_first_pole_per_ns * t_ns);
    return {_waveform_scale * (_waveform_offset + _waveform_slope * t_ns + _first_pole_weight * exp_p1),
            _waveform_scale * (_waveform_slope - _first_pole_weight * _first_pole_per_ns * exp_p1)};
  }
  return {0.0, 0.0};
}

auto DriverRampSolver::driverThresholdUpperBound() const -> double
{
  if (_algorithm == FastStaDmpAlgorithm::kPi) {
    return _ramp_start_ns + _ramp_duration_ns + (_far_cap_pf + _near_cap_pf) * (_rd_ns_per_pf + _rpi_ns_per_pf) * 2.0;
  }
  if (_algorithm == FastStaDmpAlgorithm::kZeroNearCap) {
    return _ramp_start_ns + _ramp_duration_ns + _far_cap_pf * (_rd_ns_per_pf + _rpi_ns_per_pf) * 2.0;
  }
  return _ramp_start_ns + _ramp_duration_ns;
}

auto DriverRampSolver::allFinite(const std::array<double, kMaxOrder>& values) -> bool
{
  for (const auto& value : values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

auto DriverRampSolver::allFinite(const std::array<std::array<double, kMaxOrder>, kMaxOrder>& values) -> bool
{
  for (const auto& row : values) {
    for (const auto value : row) {
      if (!std::isfinite(value)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace icts::fast_sta_dmp
