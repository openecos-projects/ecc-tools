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
#include "DmpCeffCalculator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

#include "ThresholdNormalizer.hpp"
namespace ista {
bool DmpCeffCalculator::calculate(TimingArc& timing_arc, TransType trans_type, double input_slew, const ParasiticDmpModel& dmp_model, double& gate_delay,
                                  double& driver_slew, double& effective_capacitance)
{
  if (!initialize(timing_arc, trans_type, input_slew, dmp_model)) {
    return false;
  }

  _driver_resistance = calcGateResistance();
  if (!std::isfinite(_driver_resistance) || _driver_resistance < 0.0) {
    return false;
  }
  if (_driver_resistance < kCapacitiveDriverResistance || _pi_resistance < _driver_resistance * 1E-3 || _load_capacitance == 0.0
      || _load_capacitance < _driver_capacitance * 1E-3 || _pi_resistance == 0.0) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }
  if (_driver_capacitance < _load_capacitance * 1E-3) {
    return calcZeroC2(gate_delay, driver_slew, effective_capacitance);
  }
  return calcPi(gate_delay, driver_slew, effective_capacitance);
}

bool DmpCeffCalculator::initialize(TimingArc& timing_arc, TransType trans_type, double input_slew, const ParasiticDmpModel& dmp_model)
{
  if (!dmp_model.get_is_valid() || timing_arc.get_delay_table_map().count(trans_type) == 0 || timing_arc.get_slew_table_map().count(trans_type) == 0) {
    return false;
  }

  _timing_arc = &timing_arc;
  _trans_type = trans_type;
  _input_slew = input_slew;
  _driver_capacitance = dmp_model.get_driver_capacitance();
  _pi_resistance = dmp_model.get_pi_resistance();
  _load_capacitance = dmp_model.get_load_capacitance();
  _threshold = normalizedThreshold(trans_type == TransType::kFall ? timing_arc.get_output_threshold_pct_fall() : timing_arc.get_output_threshold_pct_rise());
  _lower_threshold
      = normalizedThreshold(trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall() : timing_arc.get_slew_lower_threshold_pct_rise());
  _upper_threshold
      = normalizedThreshold(trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall() : timing_arc.get_slew_upper_threshold_pct_rise());
  _slew_derate = timing_arc.get_slew_derate();
  _is_pi = false;
  _is_zero_c2 = false;
  _newton_order = 0;
  _parameter_list.fill(0.0);
  _function_list.fill(0.0);
  _scale_list.fill(0.0);
  _delta_list.fill(0.0);
  _index_list.fill(0);
  for (std::array<double, 3>& row : _jacobian) {
    row.fill(0.0);
  }
  return std::isfinite(_input_slew) && _driver_capacitance >= 0.0 && _pi_resistance >= 0.0 && _load_capacitance >= 0.0 && _threshold > 0.0 && _threshold < 1.0
         && _lower_threshold >= 0.0 && _upper_threshold <= 1.0 && _upper_threshold > _lower_threshold && _slew_derate > 0.0;
}

double DmpCeffCalculator::calcGateResistance()
{
  double capacitance1 = _driver_capacitance + _load_capacitance;
  double capacitance2 = capacitance1 + kGateResistanceCapacitanceStep;
  double delay1 = 0.0;
  double slew1 = 0.0;
  double delay2 = 0.0;
  double slew2 = 0.0;
  if (!getGateDelaySlew(capacitance1, delay1, slew1) || !getGateDelaySlew(capacitance2, delay2, slew2)) {
    return 0.0;
  }
  return -std::log(_threshold) * std::abs(delay1 - delay2) / (capacitance2 - capacitance1);
}

bool DmpCeffCalculator::getGateDelaySlew(double capacitance, double& gate_delay, double& gate_slew)
{
  if (_timing_arc == nullptr || _timing_arc->get_delay_table_map().count(_trans_type) == 0 || _timing_arc->get_slew_table_map().count(_trans_type) == 0) {
    return false;
  }
  double converted_slew = _input_slew * _timing_arc->get_time_unit_scale();
  double converted_capacitance = capacitance * _timing_arc->get_cap_unit_scale();
  gate_delay = _timing_arc->get_delay_table_map()[_trans_type].findValue(converted_slew, converted_capacitance) / _timing_arc->get_time_unit_scale();
  gate_slew = _timing_arc->get_slew_table_map()[_trans_type].findValue(converted_slew, converted_capacitance) / _timing_arc->get_time_unit_scale();
  return std::isfinite(gate_delay) && std::isfinite(gate_slew) && gate_slew >= 0.0;
}

bool DmpCeffCalculator::calcCap(double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  effective_capacitance = _driver_capacitance + _load_capacitance;
  return getGateDelaySlew(effective_capacitance, gate_delay, driver_slew);
}

bool DmpCeffCalculator::calcPi(double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  if (!initPi()) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }

  if (!findDriverParams(_driver_capacitance + _load_capacitance) && !findDriverParams(_driver_capacitance)
      && !findDriverParams(_driver_capacitance + _load_capacitance, true) && !findDriverParams(_driver_capacitance, true)) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }

  effective_capacitance = _parameter_list[kEffectiveCapacitanceIndex];
  double table_slew = 0.0;
  if (!getGateDelaySlew(effective_capacitance, gate_delay, table_slew)) {
    return false;
  }
  double waveform_delay = 0.0;
  if (!findDriverDelaySlew(waveform_delay, driver_slew)) {
    driver_slew = table_slew;
  }
  return std::isfinite(gate_delay) && std::isfinite(driver_slew);
}

bool DmpCeffCalculator::initPi()
{
  _is_pi = true;
  _is_zero_c2 = false;
  _newton_order = 3;
  double denominator = _pi_resistance * _driver_resistance * _load_capacitance * _driver_capacitance;
  double coefficient = _driver_resistance * (_load_capacitance + _driver_capacitance) + _pi_resistance * _load_capacitance;
  double discriminant = coefficient * coefficient - 4.0 * denominator;
  if (!(denominator > 0.0) || discriminant < 0.0) {
    return false;
  }

  _pi_zero = 1.0 / (_pi_resistance * _load_capacitance);
  _pi_scale = 1.0 / (_driver_resistance * _driver_capacitance);
  double root = std::sqrt(discriminant);
  _pi_pole1 = (coefficient + root) / (2.0 * denominator);
  _pi_pole2 = (coefficient - root) / (2.0 * denominator);
  double pole_product = _pi_pole1 * _pi_pole2;
  if (!(_pi_pole1 > 0.0) || !(_pi_pole2 > 0.0) || !(pole_product > 0.0) || std::abs(_pi_pole2 - _pi_pole1) < kTinyNumber) {
    return false;
  }

  _pi_constant2 = _pi_zero / pole_product;
  _pi_constant1 = (1.0 - _pi_constant2 * (_pi_pole1 + _pi_pole2)) / pole_product;
  _pi_residue2 = (_pi_constant1 * _pi_pole1 + _pi_constant2) / (_pi_pole2 - _pi_pole1);
  _pi_residue1 = -_pi_constant1 - _pi_residue2;
  double current_zero = (_load_capacitance + _driver_capacitance) / (_pi_resistance * _load_capacitance * _driver_capacitance);
  _pi_current_constant = current_zero / pole_product;
  _pi_current_residue1 = (current_zero - _pi_pole1) / (_pi_pole1 * (_pi_pole1 - _pi_pole2));
  _pi_current_residue2 = (current_zero - _pi_pole2) / (_pi_pole2 * (_pi_pole2 - _pi_pole1));
  return std::isfinite(_pi_residue1) && std::isfinite(_pi_residue2) && std::isfinite(_pi_current_constant) && std::isfinite(_pi_current_residue1)
         && std::isfinite(_pi_current_residue2);
}

bool DmpCeffCalculator::calcZeroC2(double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  if (!initZeroC2()) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }

  effective_capacitance = _load_capacitance;
  if (findDriverParams(effective_capacitance) && findDriverDelaySlew(gate_delay, driver_slew)) {
    return true;
  }
  return getGateDelaySlew(effective_capacitance, gate_delay, driver_slew);
}

bool DmpCeffCalculator::initZeroC2()
{
  _is_pi = false;
  _is_zero_c2 = true;
  _newton_order = 2;
  if (!(_pi_resistance > 0.0) || !(_load_capacitance > 0.0) || !(_driver_resistance > 0.0)) {
    return false;
  }

  _zero_zero = 1.0 / (_pi_resistance * _load_capacitance);
  _zero_pole = 1.0 / (_load_capacitance * (_driver_resistance + _pi_resistance));
  _zero_scale = _zero_pole / _zero_zero;
  if (!(_zero_scale > 0.0) || !(_zero_pole > 0.0)) {
    return false;
  }
  _zero_constant2 = 1.0 / _zero_scale;
  _zero_constant1 = (_zero_pole - _zero_zero) / (_zero_pole * _zero_pole);
  _zero_residue = -_zero_constant1;
  return std::isfinite(_zero_constant1) && std::isfinite(_zero_constant2) && std::isfinite(_zero_residue);
}

bool DmpCeffCalculator::findDriverParams(double effective_capacitance, bool numerical_jacobian)
{
  if (_newton_order == 3) {
    _parameter_list[kEffectiveCapacitanceIndex] = effective_capacitance;
  }
  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(effective_capacitance, threshold_delay, lower_delay, measured_slew)) {
    return false;
  }

  double threshold_span = _upper_threshold - _lower_threshold;
  double transition_time = measured_slew / threshold_span;
  double start_time = threshold_delay + std::log(1.0 - _threshold) * _driver_resistance * effective_capacitance - _threshold * transition_time;
  _parameter_list[kTransitionTimeIndex] = transition_time;
  _parameter_list[kStartTimeIndex] = start_time;
  if (!(numerical_jacobian ? dampedPiNewton() : newtonRaphson())) {
    return false;
  }
  _start_time = _parameter_list[kStartTimeIndex];
  _transition_time = _parameter_list[kTransitionTimeIndex];
  return std::isfinite(_start_time) && std::isfinite(_transition_time) && _transition_time > 0.0;
}

bool DmpCeffCalculator::getGateDelays(double effective_capacitance, double& threshold_delay, double& lower_delay, double& measured_slew)
{
  double table_slew = 0.0;
  if (!getGateDelaySlew(effective_capacitance, threshold_delay, table_slew)) {
    return false;
  }
  measured_slew = table_slew * _slew_derate;
  double threshold_span = _upper_threshold - _lower_threshold;
  if (!(threshold_span > 0.0)) {
    return false;
  }
  lower_delay = threshold_delay - measured_slew * (_threshold - _lower_threshold) / threshold_span;
  return measured_slew > 0.0;
}

bool DmpCeffCalculator::newtonRaphson()
{
  for (int32_t iteration = 0; iteration < kMaxNewtonIteration; iteration++) {
    if (!evalDmpEqns()) {
      return false;
    }
    for (int32_t index = 0; index < _newton_order; index++) {
      _delta_list[index] = -_function_list[index];
    }
    if (!decomposeJacobian()) {
      return false;
    }
    solveJacobian();

    bool is_converged = true;
    for (int32_t index = 0; index < _newton_order; index++) {
      if (!std::isfinite(_delta_list[index]) || std::abs(_delta_list[index]) > std::abs(_parameter_list[index]) * kDriverParameterTolerance) {
        is_converged = false;
      }
      _parameter_list[index] += _delta_list[index];
    }
    if (is_converged) {
      return evalDmpEqns();
    }
  }
  return false;
}

bool DmpCeffCalculator::dampedPiNewton()
{
  // The fast Jacobian approximates the current equation and omits the LUT
  // dependence of the voltage sampling times on Ceff. If both fast seeds
  // fail, differentiate the actual residuals and require a decreasing norm.
  // This recovery only handles Pi models; the one-pole solver is unchanged.
  constexpr double residual_tolerance = 1e-8;
  constexpr double difference_step = 1e-5;
  const double total_capacitance = _driver_capacitance + _load_capacitance;
  auto residualNorm = [&]() {
    // Convert current to voltage so the norm is independent of current units.
    return std::hypot(_function_list[kLowerVoltageIndex], _function_list[kThresholdVoltageIndex], _function_list[kCurrentIndex] * _driver_resistance);
  };
  for (int32_t iteration = 0; iteration < kMaxNewtonIteration; ++iteration) {
    if (!evalPiEqns())
      return false;
    const double residual = residualNorm();
    if (residual < residual_tolerance)
      return true;
    const auto parameters = _parameter_list;
    const auto functions = _function_list;
    decltype(_jacobian) jacobian{};
    for (int32_t column = 0; column < kMaxNewtonOrder; ++column) {
      const double scale
          = column == kEffectiveCapacitanceIndex ? total_capacitance : std::max(std::abs(parameters[kStartTimeIndex]), parameters[kTransitionTimeIndex]);
      const double step = difference_step * scale;
      if (!(step > 0.0) || !std::isfinite(step))
        return false;
      _parameter_list = parameters;
      _parameter_list[column] += step;
      const bool plus_valid = evalPiEqns();
      const auto plus = _function_list;
      _parameter_list = parameters;
      _parameter_list[column] -= step;
      const bool minus_valid = evalPiEqns();
      const auto minus = _function_list;
      if (!plus_valid && !minus_valid)
        return false;
      for (int32_t row = 0; row < kMaxNewtonOrder; ++row) {
        // Use a one-sided difference at the physical capacitance/width bounds.
        jacobian[row][column] = plus_valid && minus_valid ? (plus[row] - minus[row]) / (2.0 * step)
                                : plus_valid              ? (plus[row] - functions[row]) / step
                                                          : (functions[row] - minus[row]) / step;
      }
    }
    _parameter_list = parameters;
    _jacobian = jacobian;
    for (int32_t row = 0; row < kMaxNewtonOrder; ++row)
      _delta_list[row] = -functions[row];
    if (!decomposeJacobian())
      return false;
    solveJacobian();

    bool accepted = false;
    for (double fraction = 1.0; fraction >= 1.0 / 4096.0; fraction *= 0.5) {
      for (int32_t index = 0; index < kMaxNewtonOrder; ++index)
        _parameter_list[index] = parameters[index] + fraction * _delta_list[index];
      if (evalPiEqns() && residualNorm() < residual) {
        accepted = true;
        break;
      }
    }
    if (!accepted)
      return false;
  }
  return evalPiEqns() && residualNorm() < residual_tolerance;
}

bool DmpCeffCalculator::evalDmpEqns()
{
  if (_is_pi) {
    return evalPiEqns();
  }
  if (_is_zero_c2) {
    return evalOnePoleEqns();
  }
  return false;
}

bool DmpCeffCalculator::evalPiEqns()
{
  double start_time = _parameter_list[kStartTimeIndex];
  double transition_time = _parameter_list[kTransitionTimeIndex];
  double effective_capacitance = _parameter_list[kEffectiveCapacitanceIndex];
  double total_capacitance = _load_capacitance + _driver_capacitance;
  if (!(effective_capacitance > 0.0) || effective_capacitance > total_capacitance || !(transition_time > 0.0)) {
    return false;
  }

  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(effective_capacitance, threshold_delay, lower_delay, measured_slew)) {
    return false;
  }
  double effective_capacitance_time = measured_slew / (_upper_threshold - _lower_threshold);
  effective_capacitance_time = std::min(effective_capacitance_time, 1.4 * transition_time);

  double threshold_voltage = 0.0;
  double lower_voltage = 0.0;
  calcCapacitiveWaveform(threshold_delay, start_time, transition_time, effective_capacitance, threshold_voltage);
  calcCapacitiveWaveform(lower_delay, start_time, transition_time, effective_capacitance, lower_voltage);
  _function_list[kCurrentIndex] = calcPiCurrentDifference(transition_time, effective_capacitance_time, effective_capacitance);
  _function_list[kThresholdVoltageIndex] = threshold_voltage - _threshold;
  _function_list[kLowerVoltageIndex] = lower_voltage - _lower_threshold;

  double exp_pole1 = calcDmpExp(-_pi_pole1 * transition_time);
  double exp_pole2 = calcDmpExp(-_pi_pole2 * transition_time);
  double exp_effective = calcDmpExp(-transition_time / (_driver_resistance * effective_capacitance));
  _jacobian[kCurrentIndex][kStartTimeIndex] = 0.0;
  _jacobian[kCurrentIndex][kTransitionTimeIndex]
      = (-_pi_current_constant * transition_time + _pi_current_residue1 * transition_time * exp_pole1
         - (2.0 * _pi_current_residue1 / _pi_pole1) * (1.0 - exp_pole1) + _pi_current_residue2 * transition_time * exp_pole2
         - (2.0 * _pi_current_residue2 / _pi_pole2) * (1.0 - exp_pole2)
         + _driver_resistance * effective_capacitance
               * (transition_time + transition_time * exp_effective - 2.0 * _driver_resistance * effective_capacitance * (1.0 - exp_effective)))
        / (_driver_resistance * transition_time * transition_time * transition_time);
  _jacobian[kCurrentIndex][kEffectiveCapacitanceIndex] = (2.0 * _driver_resistance * effective_capacitance - transition_time
                                                          - (2.0 * _driver_resistance * effective_capacitance + transition_time) * exp_effective)
                                                         / (transition_time * transition_time);

  calcCapacitiveWaveformDerivative(lower_delay, start_time, transition_time, effective_capacitance, _jacobian[kLowerVoltageIndex][kStartTimeIndex],
                                   _jacobian[kLowerVoltageIndex][kTransitionTimeIndex], _jacobian[kLowerVoltageIndex][kEffectiveCapacitanceIndex]);
  calcCapacitiveWaveformDerivative(threshold_delay, start_time, transition_time, effective_capacitance, _jacobian[kThresholdVoltageIndex][kStartTimeIndex],
                                   _jacobian[kThresholdVoltageIndex][kTransitionTimeIndex], _jacobian[kThresholdVoltageIndex][kEffectiveCapacitanceIndex]);
  return std::isfinite(_function_list[kCurrentIndex]) && std::isfinite(_function_list[kThresholdVoltageIndex])
         && std::isfinite(_function_list[kLowerVoltageIndex]);
}

double DmpCeffCalculator::calcPiCurrentDifference(double transition_time, double effective_capacitance_time, double effective_capacitance)
{
  double exp_pole1 = calcDmpExp(-_pi_pole1 * effective_capacitance_time);
  double exp_pole2 = calcDmpExp(-_pi_pole2 * effective_capacitance_time);
  double exp_effective = calcDmpExp(-effective_capacitance_time / (_driver_resistance * effective_capacitance));
  double pi_current = (_pi_current_constant * effective_capacitance_time + (_pi_current_residue1 / _pi_pole1) * (1.0 - exp_pole1)
                       + (_pi_current_residue2 / _pi_pole2) * (1.0 - exp_pole2))
                      / (_driver_resistance * effective_capacitance_time * transition_time);
  double effective_current = (_driver_resistance * effective_capacitance * effective_capacitance_time
                              - std::pow(_driver_resistance * effective_capacitance, 2) * (1.0 - exp_effective))
                             / (_driver_resistance * effective_capacitance_time * transition_time);
  return pi_current - effective_current;
}

bool DmpCeffCalculator::evalOnePoleEqns()
{
  double start_time = _parameter_list[kStartTimeIndex];
  double transition_time = _parameter_list[kTransitionTimeIndex];
  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(_load_capacitance, threshold_delay, lower_delay, measured_slew) || !(transition_time > 0.0)) {
    return false;
  }

  double threshold_voltage = 0.0;
  double lower_voltage = 0.0;
  calcCapacitiveWaveform(threshold_delay, start_time, transition_time, _load_capacitance, threshold_voltage);
  calcCapacitiveWaveform(lower_delay, start_time, transition_time, _load_capacitance, lower_voltage);
  _function_list[kThresholdVoltageIndex] = threshold_voltage - _threshold;
  _function_list[kLowerVoltageIndex] = lower_voltage - _lower_threshold;

  double ignored_capacitance_derivative = 0.0;
  calcCapacitiveWaveformDerivative(lower_delay, start_time, transition_time, _load_capacitance, _jacobian[kLowerVoltageIndex][kStartTimeIndex],
                                   _jacobian[kLowerVoltageIndex][kTransitionTimeIndex], ignored_capacitance_derivative);
  calcCapacitiveWaveformDerivative(threshold_delay, start_time, transition_time, _load_capacitance, _jacobian[kThresholdVoltageIndex][kStartTimeIndex],
                                   _jacobian[kThresholdVoltageIndex][kTransitionTimeIndex], ignored_capacitance_derivative);
  return std::isfinite(_function_list[kThresholdVoltageIndex]) && std::isfinite(_function_list[kLowerVoltageIndex]);
}

void DmpCeffCalculator::calcCapacitiveWaveform(double time, double start_time, double transition_time, double capacitance, double& voltage)
{
  double shifted_time = time - start_time;
  if (shifted_time <= 0.0) {
    voltage = 0.0;
  } else if (shifted_time <= transition_time) {
    voltage = calcCapacitiveUnitRamp(shifted_time, capacitance) / transition_time;
  } else {
    voltage = (calcCapacitiveUnitRamp(shifted_time, capacitance) - calcCapacitiveUnitRamp(shifted_time - transition_time, capacitance)) / transition_time;
  }
}

double DmpCeffCalculator::calcCapacitiveUnitRamp(double time, double capacitance)
{
  double time_constant = _driver_resistance * capacitance;
  if (!(time_constant > 0.0)) {
    return time;
  }
  return time - time_constant * (1.0 - calcDmpExp(-time / time_constant));
}

void DmpCeffCalculator::calcCapacitiveWaveformDerivative(double time, double start_time, double transition_time, double capacitance, double& start_derivative,
                                                         double& transition_derivative, double& capacitance_derivative)
{
  double shifted_time = time - start_time;
  if (shifted_time <= 0.0) {
    start_derivative = 0.0;
    transition_derivative = 0.0;
    capacitance_derivative = 0.0;
  } else if (shifted_time <= transition_time) {
    start_derivative = -calcCapacitiveUnitRampTimeDerivative(shifted_time, capacitance) / transition_time;
    transition_derivative = -calcCapacitiveUnitRamp(shifted_time, capacitance) / (transition_time * transition_time);
    capacitance_derivative = calcCapacitiveUnitRampCapDerivative(shifted_time, capacitance) / transition_time;
  } else {
    start_derivative
        = -(calcCapacitiveUnitRampTimeDerivative(shifted_time, capacitance) - calcCapacitiveUnitRampTimeDerivative(shifted_time - transition_time, capacitance))
          / transition_time;
    transition_derivative = -(calcCapacitiveUnitRamp(shifted_time, capacitance) - calcCapacitiveUnitRamp(shifted_time - transition_time, capacitance))
                                / (transition_time * transition_time)
                            + calcCapacitiveUnitRampTimeDerivative(shifted_time - transition_time, capacitance) / transition_time;
    capacitance_derivative
        = (calcCapacitiveUnitRampCapDerivative(shifted_time, capacitance) - calcCapacitiveUnitRampCapDerivative(shifted_time - transition_time, capacitance))
          / transition_time;
  }
}

double DmpCeffCalculator::calcCapacitiveUnitRampTimeDerivative(double time, double capacitance)
{
  double time_constant = _driver_resistance * capacitance;
  if (!(time_constant > 0.0)) {
    return 1.0;
  }
  return 1.0 - calcDmpExp(-time / time_constant);
}

double DmpCeffCalculator::calcCapacitiveUnitRampCapDerivative(double time, double capacitance)
{
  double time_constant = _driver_resistance * capacitance;
  if (!(time_constant > 0.0)) {
    return 0.0;
  }
  return _driver_resistance * ((1.0 + time / time_constant) * calcDmpExp(-time / time_constant) - 1.0);
}

bool DmpCeffCalculator::decomposeJacobian()
{
  for (int32_t row = 0; row < _newton_order; row++) {
    double largest_value = 0.0;
    for (int32_t column = 0; column < _newton_order; column++) {
      largest_value = std::max(largest_value, std::abs(_jacobian[row][column]));
    }
    if (largest_value == 0.0 || !std::isfinite(largest_value)) {
      return false;
    }
    _scale_list[row] = 1.0 / largest_value;
  }

  int32_t last_index = _newton_order - 1;
  for (int32_t column = 0; column < _newton_order; column++) {
    for (int32_t row = 0; row < column; row++) {
      double value = _jacobian[row][column];
      for (int32_t index = 0; index < row; index++) {
        value -= _jacobian[row][index] * _jacobian[index][column];
      }
      _jacobian[row][column] = value;
    }

    double largest_value = 0.0;
    int32_t pivot_row = column;
    for (int32_t row = column; row < _newton_order; row++) {
      double value = _jacobian[row][column];
      for (int32_t index = 0; index < column; index++) {
        value -= _jacobian[row][index] * _jacobian[index][column];
      }
      _jacobian[row][column] = value;
      double scaled_value = _scale_list[row] * std::abs(value);
      if (scaled_value >= largest_value) {
        largest_value = scaled_value;
        pivot_row = row;
      }
    }
    if (column != pivot_row) {
      std::swap(_jacobian[pivot_row], _jacobian[column]);
      _scale_list[pivot_row] = _scale_list[column];
    }
    _index_list[column] = pivot_row;
    if (_jacobian[column][column] == 0.0) {
      _jacobian[column][column] = kTinyNumber;
    }
    if (column != last_index) {
      double inverse_pivot = 1.0 / _jacobian[column][column];
      for (int32_t row = column + 1; row < _newton_order; row++) {
        _jacobian[row][column] *= inverse_pivot;
      }
    }
  }
  return true;
}

void DmpCeffCalculator::solveJacobian()
{
  int32_t first_nonzero = -1;
  for (int32_t row = 0; row < _newton_order; row++) {
    int32_t pivot_row = _index_list[row];
    double value = _delta_list[pivot_row];
    _delta_list[pivot_row] = _delta_list[row];
    if (first_nonzero != -1) {
      for (int32_t column = first_nonzero; column < row; column++) {
        value -= _jacobian[row][column] * _delta_list[column];
      }
    } else if (value != 0.0) {
      first_nonzero = row;
    }
    _delta_list[row] = value;
  }

  for (int32_t row = _newton_order - 1; row >= 0; row--) {
    double value = _delta_list[row];
    for (int32_t column = row + 1; column < _newton_order; column++) {
      value -= _jacobian[row][column] * _delta_list[column];
    }
    _delta_list[row] = value / _jacobian[row][row];
  }
}

bool DmpCeffCalculator::findDriverDelaySlew(double& driver_delay, double& driver_slew)
{
  double upper_bound = getOutputCrossingUpperBound();
  double lower_crossing = 0.0;
  double upper_crossing = 0.0;
  if (!(upper_bound > _start_time) || !findOutputCrossing(_threshold, _start_time, upper_bound, driver_delay)
      || !findOutputCrossing(_lower_threshold, _start_time, driver_delay, lower_crossing)
      || !findOutputCrossing(_upper_threshold, driver_delay, upper_bound, upper_crossing)) {
    return false;
  }
  driver_slew = (upper_crossing - lower_crossing) / _slew_derate;
  return std::isfinite(driver_delay) && std::isfinite(driver_slew) && driver_slew >= 0.0;
}

bool DmpCeffCalculator::findOutputCrossing(double threshold, double lower_time, double upper_time, double& crossing_time)
{
  std::function<void(double, double&, double&)> waveform_function = [this, threshold](double time, double& value, double& derivative) {
    calcOutputWaveform(time, value, derivative);
    value -= threshold;
  };
  return findRoot(waveform_function, lower_time, upper_time, crossing_time);
}

bool DmpCeffCalculator::findRoot(std::function<void(double, double&, double&)>& function, double lower_time, double upper_time, double& root)
{
  double lower_value = 0.0;
  double upper_value = 0.0;
  double ignored_derivative = 0.0;
  function(lower_time, lower_value, ignored_derivative);
  function(upper_time, upper_value, ignored_derivative);
  if (!std::isfinite(lower_value) || !std::isfinite(upper_value) || lower_value * upper_value > 0.0) {
    return false;
  }
  if (lower_value == 0.0) {
    root = lower_time;
    return true;
  }
  if (upper_value == 0.0) {
    root = upper_time;
    return true;
  }
  if (lower_value > 0.0) {
    std::swap(lower_time, upper_time);
  }

  root = 0.5 * (lower_time + upper_time);
  double previous_delta = std::abs(upper_time - lower_time);
  double delta = previous_delta;
  double value = 0.0;
  double derivative = 0.0;
  function(root, value, derivative);
  for (int32_t iteration = 0; iteration < kMaxRootIteration; iteration++) {
    bool use_bisection = derivative == 0.0 || (((root - upper_time) * derivative - value) * ((root - lower_time) * derivative - value) > 0.0)
                         || std::abs(2.0 * value) > std::abs(previous_delta * derivative);
    if (use_bisection) {
      previous_delta = delta;
      delta = 0.5 * (upper_time - lower_time);
      root = lower_time + delta;
    } else {
      previous_delta = delta;
      delta = value / derivative;
      root -= delta;
    }
    if (std::abs(delta) <= kThresholdTimeTolerance * std::abs(root)) {
      return true;
    }

    function(root, value, derivative);
    if (!std::isfinite(value) || !std::isfinite(derivative)) {
      return false;
    }
    if (value < 0.0) {
      lower_time = root;
    } else {
      upper_time = root;
    }
  }
  return false;
}

void DmpCeffCalculator::calcOutputWaveform(double time, double& voltage, double& derivative)
{
  double shifted_time = time - _start_time;
  if (shifted_time <= 0.0) {
    voltage = 0.0;
    derivative = 0.0;
    return;
  }

  double unit_voltage = 0.0;
  double unit_derivative = 0.0;
  if (shifted_time <= _transition_time) {
    if (_is_pi) {
      calcPiUnitRamp(shifted_time, unit_voltage, unit_derivative);
    } else {
      calcZeroC2UnitRamp(shifted_time, unit_voltage, unit_derivative);
    }
    voltage = unit_voltage / _transition_time;
    derivative = unit_derivative / _transition_time;
    return;
  }

  double delayed_voltage = 0.0;
  double delayed_derivative = 0.0;
  if (_is_pi) {
    calcPiUnitRamp(shifted_time, unit_voltage, unit_derivative);
    calcPiUnitRamp(shifted_time - _transition_time, delayed_voltage, delayed_derivative);
  } else {
    calcZeroC2UnitRamp(shifted_time, unit_voltage, unit_derivative);
    calcZeroC2UnitRamp(shifted_time - _transition_time, delayed_voltage, delayed_derivative);
  }
  voltage = (unit_voltage - delayed_voltage) / _transition_time;
  derivative = (unit_derivative - delayed_derivative) / _transition_time;
}

void DmpCeffCalculator::calcPiUnitRamp(double time, double& voltage, double& derivative)
{
  double exp_pole1 = calcDmpExp(-_pi_pole1 * time);
  double exp_pole2 = calcDmpExp(-_pi_pole2 * time);
  voltage = _pi_scale * (_pi_constant1 + _pi_constant2 * time + _pi_residue1 * exp_pole1 + _pi_residue2 * exp_pole2);
  derivative = _pi_scale * (_pi_constant2 - _pi_residue1 * _pi_pole1 * exp_pole1 - _pi_residue2 * _pi_pole2 * exp_pole2);
}

void DmpCeffCalculator::calcZeroC2UnitRamp(double time, double& voltage, double& derivative)
{
  double exp_pole = calcDmpExp(-_zero_pole * time);
  voltage = _zero_scale * (_zero_constant1 + _zero_constant2 * time + _zero_residue * exp_pole);
  derivative = _zero_scale * (_zero_constant2 - _zero_residue * _zero_pole * exp_pole);
}

double DmpCeffCalculator::getOutputCrossingUpperBound()
{
  if (_is_pi) {
    return _start_time + _transition_time + (_load_capacitance + _driver_capacitance) * (_driver_resistance + _pi_resistance) * 2.0;
  }
  if (_is_zero_c2) {
    return _start_time + _transition_time + _load_capacitance * (_driver_resistance + _pi_resistance) * 2.0;
  }
  return _start_time + _transition_time;
}

double DmpCeffCalculator::calcDmpExp(double value)
{
  if (value < -12.0) {
    return 0.0;
  }
  double result = 1.0 + value / 4096.0;
  for (int32_t iteration = 0; iteration < 12; iteration++) {
    result *= result;
  }
  return result;
}

}  // namespace ista
