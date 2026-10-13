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
 * @file FastSTADmpCeffSolver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief DMP effective-capacitance solver state and equation declarations for CTS fast STA.
 */

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>

#include "liberty/FastSTALibertyModel.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts {

struct FastStaPiModel;

}  // namespace icts

namespace icts::fast_sta_dmp {

constexpr double kOhmPfToNs = 1e-3;
constexpr double kCapDeltaPf = 1e-3;
constexpr double kSmallRdNsPerPf = 1e-5;
constexpr double kEpsilon = 1e-18;
constexpr double kDriverParamTolerance = 0.01;
constexpr double kThresholdTimeTolerance = 0.01;
constexpr std::size_t kFindRootMaxIter = 20U;
constexpr std::size_t kNewtonRaphsonMaxIter = 100U;
constexpr std::size_t kMaxOrder = 3U;

enum class DriverVariable : std::size_t
{
  kRampStart = 0U,
  kRampDuration = 1U,
  kEffectiveCapacitance = 2U
};

enum class DriverEquation : std::size_t
{
  kSlewLowerCrossing = 0U,
  kDelayCrossing = 1U,
  kAverageCurrentBalance = 2U
};

constexpr auto ToIndex(DriverVariable param) -> std::size_t
{
  return static_cast<std::size_t>(param);
}

constexpr auto ToIndex(DriverEquation func) -> std::size_t
{
  return static_cast<std::size_t>(func);
}

struct GateThresholdTiming
{
  bool valid = false;
  double delay_ns = 0.0;
  double table_slew_ns = 0.0;
  double measured_slew_ns = 0.0;
  double lower_threshold_time_ns = 0.0;
};

using RootFunc = std::function<void(double, double&, double&)>;

auto OutputThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double;
auto InputThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double;
auto SlewLowerThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double;
auto SlewUpperThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double;
auto SlewDerate(const FastStaLibertyCell& cell) -> double;
auto DmpExp(double x) -> double;
auto FindRoot(const RootFunc& func, double x1, double x2, double x_tolerance, std::size_t max_iter) -> std::optional<double>;

class DriverRampSolver
{
 public:
  DriverRampSolver(const FastStaLibertyCell& cell, const FastStaLibertyArc& arc, const FastStaPiModel& pi, FastStaTransition transition, double input_slew_ns);

  auto solve() -> FastStaDmpDriverResult;

 private:
  auto lookupGateTiming(double ceff_pf) const -> std::optional<std::pair<double, double>>;
  auto estimateDriverResistanceNsPerPf(const FastStaPiModel& pi) const -> double;
  auto useCapAlgorithm() const -> bool;
  auto makeResult(FastStaDmpAlgorithm algorithm, double ceff_pf, double delay_ns, double slew_ns, bool waveform_valid, double waveform_delay_ns = 0.0) const
      -> FastStaDmpDriverResult;
  auto solveCap() -> FastStaDmpDriverResult;
  auto solvePi() -> FastStaDmpDriverResult;
  auto solveZeroNearCap() -> FastStaDmpDriverResult;
  auto initPi() -> bool;
  auto initZeroNearCap() -> bool;
  auto solveRampParameters(double ceff_seed_pf) -> bool;
  auto evaluateGateThresholds(double ceff_pf) const -> GateThresholdTiming;
  auto capacitiveRampResponse(double t_ns, double ramp_start_ns, double ramp_duration_ns, double load_cap_pf) const -> std::pair<double, double>;
  auto capacitiveRampIntegral(double t_ns, double load_cap_pf) const -> double;
  auto capacitiveRampSlope(double t_ns, double load_cap_pf) const -> double;
  auto capacitiveRampCapDerivative(double t_ns, double load_cap_pf) const -> double;
  auto capacitiveRampDerivatives(double t_ns, double ramp_start_ns, double ramp_duration_ns, double load_cap_pf) const -> std::tuple<double, double, double>;
  auto evaluateResiduals() -> bool;
  auto evaluatePiResiduals() -> bool;
  auto evaluateSinglePoleResiduals() -> bool;
  auto averageCurrentResidual(double ramp_duration_ns, double ceff_time_ns, double ceff_pf) const -> double;
  auto solveNonlinearSystem() -> bool;
  auto factorJacobian() -> bool;
  auto solveNewtonStep() -> bool;
  auto measureDriverThresholds() -> std::optional<std::pair<double, double>>;
  auto findDriverThresholdTime(double threshold, double lower, double upper) -> std::optional<double>;
  auto driverRampResponse(double t_ns) const -> std::pair<double, double>;
  auto driverRampIntegral(double t_ns) const -> std::pair<double, double>;
  auto driverThresholdUpperBound() const -> double;

  static auto allFinite(const std::array<double, kMaxOrder>& values) -> bool;
  static auto allFinite(const std::array<std::array<double, kMaxOrder>, kMaxOrder>& values) -> bool;

  const FastStaLibertyCell* _cell = nullptr;
  const FastStaLibertyArc* _arc = nullptr;
  const FastStaLibertyTable* _delay_table = nullptr;
  const FastStaLibertyTable* _slew_table = nullptr;
  FastStaTransition _transition = FastStaTransition::kRise;
  double _input_slew_ns = 0.0;
  double _near_cap_pf = 0.0;
  double _far_cap_pf = 0.0;
  double _rpi_ns_per_pf = 0.0;
  double _rd_ns_per_pf = 0.0;
  double _output_threshold = 0.5;
  double _slew_lower_threshold = 0.3;
  double _slew_upper_threshold = 0.7;
  double _slew_derate = 1.0;
  FastStaDmpAlgorithm _algorithm = FastStaDmpAlgorithm::kCap;
  std::size_t _active_variable_count = 1U;
  double _ramp_start_ns = 0.0;
  double _ramp_duration_ns = 0.0;
  double _effective_capacitance_pf = 0.0;
  std::array<double, kMaxOrder> _variables{};
  std::array<double, kMaxOrder> _residuals{};
  std::array<std::array<double, kMaxOrder>, kMaxOrder> _jacobian{};
  std::array<double, kMaxOrder> _row_scales{};
  std::array<double, kMaxOrder> _newton_step{};
  std::array<std::size_t, kMaxOrder> _pivot_rows{};
  double _first_pole_per_ns = 0.0;
  double _second_pole_per_ns = 0.0;
  double _transfer_zero_per_ns = 0.0;
  double _waveform_scale = 0.0;
  double _waveform_offset = 0.0;
  double _waveform_slope = 0.0;
  double _first_pole_weight = 0.0;
  double _second_pole_weight = 0.0;
  double _steady_current_coefficient = 0.0;
  double _first_pole_current_coefficient = 0.0;
  double _second_pole_current_coefficient = 0.0;
};

}  // namespace icts::fast_sta_dmp
