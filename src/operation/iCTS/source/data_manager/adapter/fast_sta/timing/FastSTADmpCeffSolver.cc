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
 * @file FastSTADmpCeffSolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief DMP effective-capacitance solver setup for CTS fast STA.
 */

#include "FastSTADmpCeffSolver.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FastSTALibertyModel.hh"
#include "clock_net_parasitic/FastSTAClockNetParasitic.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts::fast_sta_dmp {

namespace {

auto selectTable(const std::vector<FastStaLibertyTable>& tables, FastStaTransition transition) -> const FastStaLibertyTable*
{
  for (const auto& table : tables) {
    if (table.transition == transition && table.valid()) {
      return &table;
    }
  }
  return nullptr;
}

}  // namespace

DriverRampSolver::DriverRampSolver(const FastStaLibertyCell& cell, const FastStaLibertyArc& arc, const FastStaPiModel& pi, FastStaTransition transition,
                                   double input_slew_ns)
    : _cell(&cell),
      _arc(&arc),
      _delay_table(selectTable(arc.delay_tables, transition)),
      _slew_table(selectTable(arc.slew_tables, transition)),
      _transition(transition),
      _input_slew_ns(std::max(0.0, input_slew_ns)),
      _near_cap_pf(std::max(0.0, pi.near_cap_pf)),
      _far_cap_pf(std::max(0.0, pi.far_cap_pf)),
      _rpi_ns_per_pf(std::max(0.0, pi.resistance_ohm) * kOhmPfToNs),
      _rd_ns_per_pf(estimateDriverResistanceNsPerPf(pi)),
      _output_threshold(OutputThreshold(cell, transition)),
      _slew_lower_threshold(SlewLowerThreshold(cell, transition)),
      _slew_upper_threshold(SlewUpperThreshold(cell, transition)),
      _slew_derate(SlewDerate(cell))
{
}

auto DriverRampSolver::lookupGateTiming(double ceff_pf) const -> std::optional<std::pair<double, double>>
{
  if (_delay_table == nullptr || _slew_table == nullptr) {
    return std::nullopt;
  }
  const auto delay_ns = _delay_table->lookupValidated(_input_slew_ns, ceff_pf);
  const auto slew_ns = _slew_table->lookupValidated(_input_slew_ns, ceff_pf);
  if (!delay_ns.has_value() || !slew_ns.has_value()) {
    return std::nullopt;
  }
  return std::pair<double, double>{*delay_ns, *slew_ns};
}

auto DriverRampSolver::estimateDriverResistanceNsPerPf(const FastStaPiModel& pi) const -> double
{
  const auto cap1_pf = std::max(0.0, pi.near_cap_pf + pi.far_cap_pf);
  const auto cap2_pf = cap1_pf + kCapDeltaPf;
  const auto gate_values1 = lookupGateTiming(cap1_pf);
  const auto gate_values2 = lookupGateTiming(cap2_pf);
  if (!gate_values1.has_value() || !gate_values2.has_value()) {
    return 0.0;
  }
  const double delay1_ns = gate_values1->first;
  const double delay2_ns = gate_values2->first;
  const auto vth = OutputThreshold(*_cell, _transition);
  if (!std::isfinite(delay1_ns) || !std::isfinite(delay2_ns) || vth <= 0.0) {
    return 0.0;
  }
  return -std::log(vth) * std::abs(delay1_ns - delay2_ns) / kCapDeltaPf;
}

auto DriverRampSolver::solve() -> FastStaDmpDriverResult
{
  if (useCapAlgorithm()) {
    return solveCap();
  }
  if (_near_cap_pf < _far_cap_pf * 1e-3) {
    return solveZeroNearCap();
  }
  return solvePi();
}

auto DriverRampSolver::useCapAlgorithm() const -> bool
{
  return _rd_ns_per_pf < kSmallRdNsPerPf || _rpi_ns_per_pf < _rd_ns_per_pf * 1e-3 || _far_cap_pf == 0.0 || _far_cap_pf < _near_cap_pf * 1e-3
         || _rpi_ns_per_pf == 0.0;
}

auto DriverRampSolver::makeResult(FastStaDmpAlgorithm algorithm, double ceff_pf, double delay_ns, double slew_ns, bool waveform_valid,
                                  double waveform_delay_ns) const -> FastStaDmpDriverResult
{
  return FastStaDmpDriverResult{
      .valid = true,
      .driver_waveform_valid = waveform_valid,
      .algorithm = algorithm,
      .transition = _transition,
      .driver_library_name = _arc->library_name.empty() ? _cell->library_name : _arc->library_name,
      .driver_cell_master = _cell->cell_master,
      .ceff_pf = std::max(0.0, ceff_pf),
      .gate_delay_ns = std::max(0.0, delay_ns),
      .driver_slew_ns = std::max(0.0, slew_ns),
      .driver_waveform_delay_ns = std::max(0.0, waveform_delay_ns),
      .ramp_start_ns = _ramp_start_ns,
      .ramp_duration_ns = std::max(0.0, _ramp_duration_ns),
      .near_cap_pf = _near_cap_pf,
      .far_cap_pf = _far_cap_pf,
      .rpi_ns_per_pf = _rpi_ns_per_pf,
      .rd_ns_per_pf = _rd_ns_per_pf,
      .input_threshold = InputThreshold(*_cell, _transition),
      .output_threshold = _output_threshold,
      .slew_lower_threshold = _slew_lower_threshold,
      .slew_upper_threshold = _slew_upper_threshold,
      .slew_derate = _slew_derate,
      .pole1_per_ns = _first_pole_per_ns,
      .pole2_per_ns = _second_pole_per_ns,
      .zero1_per_ns = _transfer_zero_per_ns,
      .waveform_scale = _waveform_scale,
      .waveform_offset = _waveform_offset,
      .waveform_slope = _waveform_slope,
      .first_pole_weight = _first_pole_weight,
      .second_pole_weight = _second_pole_weight,
  };
}

auto DriverRampSolver::solveCap() -> FastStaDmpDriverResult
{
  const auto ceff_pf = _near_cap_pf + _far_cap_pf;
  const auto gate_values = lookupGateTiming(ceff_pf);
  if (!gate_values.has_value()) {
    return {};
  }
  const auto [delay_ns, slew_ns] = *gate_values;
  return makeResult(FastStaDmpAlgorithm::kCap, ceff_pf, delay_ns, slew_ns, false);
}

auto DriverRampSolver::solvePi() -> FastStaDmpDriverResult
{
  _algorithm = FastStaDmpAlgorithm::kPi;
  _active_variable_count = 3U;
  if (!initPi()) {
    return solveCap();
  }

  if (!solveRampParameters(_near_cap_pf + _far_cap_pf) && !solveRampParameters(_near_cap_pf)) {
    return solveCap();
  }
  _effective_capacitance_pf = _variables.at(ToIndex(DriverVariable::kEffectiveCapacitance));
  const auto gate_values = lookupGateTiming(_effective_capacitance_pf);
  if (!gate_values.has_value()) {
    return {};
  }
  const auto [table_delay_ns, table_slew_ns] = *gate_values;
  const auto driver_crossing = measureDriverThresholds();
  if (!driver_crossing.has_value()) {
    return makeResult(FastStaDmpAlgorithm::kPi, _effective_capacitance_pf, table_delay_ns, table_slew_ns, false);
  }
  const auto [waveform_delay_ns, waveform_slew_ns] = *driver_crossing;
  return makeResult(FastStaDmpAlgorithm::kPi, _effective_capacitance_pf, table_delay_ns, waveform_slew_ns, true, waveform_delay_ns);
}

auto DriverRampSolver::solveZeroNearCap() -> FastStaDmpDriverResult
{
  _algorithm = FastStaDmpAlgorithm::kZeroNearCap;
  _active_variable_count = 2U;
  _effective_capacitance_pf = _far_cap_pf;
  if (!initZeroNearCap() || !solveRampParameters(_far_cap_pf)) {
    const auto gate_values = lookupGateTiming(_far_cap_pf);
    if (!gate_values.has_value()) {
      return {};
    }
    const auto [delay_ns, slew_ns] = *gate_values;
    return makeResult(FastStaDmpAlgorithm::kZeroNearCap, _far_cap_pf, delay_ns, slew_ns, false);
  }

  const auto driver_crossing = measureDriverThresholds();
  if (!driver_crossing.has_value()) {
    const auto gate_values = lookupGateTiming(_far_cap_pf);
    if (!gate_values.has_value()) {
      return {};
    }
    const auto [delay_ns, slew_ns] = *gate_values;
    return makeResult(FastStaDmpAlgorithm::kZeroNearCap, _far_cap_pf, delay_ns, slew_ns, false);
  }
  const auto [waveform_delay_ns, waveform_slew_ns] = *driver_crossing;
  return makeResult(FastStaDmpAlgorithm::kZeroNearCap, _far_cap_pf, waveform_delay_ns, waveform_slew_ns, true, waveform_delay_ns);
}

auto DriverRampSolver::initPi() -> bool
{
  if (_near_cap_pf <= 0.0 || _far_cap_pf <= 0.0 || _rpi_ns_per_pf <= 0.0 || _rd_ns_per_pf <= 0.0) {
    return false;
  }
  _transfer_zero_per_ns = 1.0 / (_rpi_ns_per_pf * _far_cap_pf);
  _waveform_scale = 1.0 / (_rd_ns_per_pf * _near_cap_pf);
  const auto a = _rpi_ns_per_pf * _rd_ns_per_pf * _far_cap_pf * _near_cap_pf;
  const auto b = _rd_ns_per_pf * (_far_cap_pf + _near_cap_pf) + _rpi_ns_per_pf * _far_cap_pf;
  const auto discriminant = b * b - 4.0 * a;
  if (a <= 0.0 || discriminant < 0.0) {
    return false;
  }
  const auto root = std::sqrt(discriminant);
  _first_pole_per_ns = (b + root) / (2.0 * a);
  _second_pole_per_ns = (b - root) / (2.0 * a);
  const auto p1p2 = _first_pole_per_ns * _second_pole_per_ns;
  if (std::abs(p1p2) <= kEpsilon || std::abs(_second_pole_per_ns - _first_pole_per_ns) <= kEpsilon) {
    return false;
  }

  _waveform_slope = _transfer_zero_per_ns / p1p2;
  _waveform_offset = (1.0 - _waveform_slope * (_first_pole_per_ns + _second_pole_per_ns)) / p1p2;
  _second_pole_weight = (_waveform_offset * _first_pole_per_ns + _waveform_slope) / (_second_pole_per_ns - _first_pole_per_ns);
  _first_pole_weight = -_waveform_offset - _second_pole_weight;

  const auto z = (_far_cap_pf + _near_cap_pf) / (_rpi_ns_per_pf * _far_cap_pf * _near_cap_pf);
  _steady_current_coefficient = z / p1p2;
  _first_pole_current_coefficient = (z - _first_pole_per_ns) / (_first_pole_per_ns * (_first_pole_per_ns - _second_pole_per_ns));
  _second_pole_current_coefficient = (z - _second_pole_per_ns) / (_second_pole_per_ns * (_second_pole_per_ns - _first_pole_per_ns));
  return std::isfinite(_steady_current_coefficient) && std::isfinite(_first_pole_current_coefficient) && std::isfinite(_second_pole_current_coefficient);
}

auto DriverRampSolver::initZeroNearCap() -> bool
{
  if (_far_cap_pf <= 0.0 || _rpi_ns_per_pf <= 0.0 || _rd_ns_per_pf <= 0.0) {
    return false;
  }
  _transfer_zero_per_ns = 1.0 / (_rpi_ns_per_pf * _far_cap_pf);
  _first_pole_per_ns = 1.0 / (_far_cap_pf * (_rd_ns_per_pf + _rpi_ns_per_pf));
  if (std::abs(_transfer_zero_per_ns) <= kEpsilon || std::abs(_first_pole_per_ns) <= kEpsilon) {
    return false;
  }
  _waveform_scale = _first_pole_per_ns / _transfer_zero_per_ns;
  _waveform_slope = 1.0 / _waveform_scale;
  _waveform_offset = (_first_pole_per_ns - _transfer_zero_per_ns) / (_first_pole_per_ns * _first_pole_per_ns);
  _first_pole_weight = -_waveform_offset;
  _second_pole_weight = 0.0;
  return std::isfinite(_waveform_scale) && std::isfinite(_waveform_offset) && std::isfinite(_waveform_slope) && std::isfinite(_first_pole_weight);
}

auto DriverRampSolver::solveRampParameters(double ceff_seed_pf) -> bool
{
  if (_active_variable_count == 3U) {
    _variables.at(ToIndex(DriverVariable::kEffectiveCapacitance)) = ceff_seed_pf;
  }
  const auto gate_values = evaluateGateThresholds(ceff_seed_pf);
  if (!gate_values.valid) {
    return false;
  }
  const auto threshold_span = _slew_upper_threshold - _slew_lower_threshold;
  if (threshold_span <= kEpsilon || _rd_ns_per_pf <= 0.0 || ceff_seed_pf <= 0.0) {
    return false;
  }
  const auto ramp_duration_ns = gate_values.measured_slew_ns / threshold_span;
  if (ramp_duration_ns <= kEpsilon) {
    return false;
  }
  const auto ramp_start_ns = gate_values.delay_ns + std::log(1.0 - _output_threshold) * _rd_ns_per_pf * ceff_seed_pf - _output_threshold * ramp_duration_ns;
  _variables.at(ToIndex(DriverVariable::kRampDuration)) = ramp_duration_ns;
  _variables.at(ToIndex(DriverVariable::kRampStart)) = ramp_start_ns;
  if (!solveNonlinearSystem()) {
    return false;
  }
  _ramp_start_ns = _variables.at(ToIndex(DriverVariable::kRampStart));
  _ramp_duration_ns = _variables.at(ToIndex(DriverVariable::kRampDuration));
  if (_active_variable_count == 3U) {
    _effective_capacitance_pf = _variables.at(ToIndex(DriverVariable::kEffectiveCapacitance));
  }
  return std::isfinite(_ramp_start_ns) && std::isfinite(_ramp_duration_ns) && _ramp_duration_ns > 0.0 && _effective_capacitance_pf >= 0.0;
}

auto DriverRampSolver::evaluateGateThresholds(double ceff_pf) const -> GateThresholdTiming
{
  const auto gate_values = lookupGateTiming(ceff_pf);
  if (!gate_values.has_value()) {
    return {};
  }
  const auto [delay_ns, table_slew_ns] = *gate_values;
  const auto threshold_span = _slew_upper_threshold - _slew_lower_threshold;
  if (!std::isfinite(delay_ns) || !std::isfinite(table_slew_ns) || table_slew_ns <= 0.0 || threshold_span <= kEpsilon) {
    return GateThresholdTiming{};
  }
  const auto measured_slew_ns = table_slew_ns * _slew_derate;
  return GateThresholdTiming{.valid = true,
                             .delay_ns = delay_ns,
                             .table_slew_ns = table_slew_ns,
                             .measured_slew_ns = measured_slew_ns,
                             .lower_threshold_time_ns = delay_ns - measured_slew_ns * (_output_threshold - _slew_lower_threshold) / threshold_span};
}

}  // namespace icts::fast_sta_dmp
