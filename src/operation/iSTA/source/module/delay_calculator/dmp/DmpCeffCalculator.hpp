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
#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "ParasiticDmpModel.hpp"
#include "liberty/TimingArc.hpp"
namespace ista {
// One solve owns its Newton/Jacobian and waveform state. No database or shared cache.
class DmpCeffCalculator
{
 public:
  bool calculate(TimingArc& timing_arc, TransType trans_type, double input_slew, const ParasiticDmpModel& dmp_model, double& gate_delay, double& driver_slew,
                 double& effective_capacitance);

 private:
  bool initialize(TimingArc& timing_arc, TransType trans_type, double input_slew, const ParasiticDmpModel& dmp_model);
  double calcGateResistance();
  bool getGateDelaySlew(double capacitance, double& gate_delay, double& gate_slew);
  bool calcCap(double& gate_delay, double& driver_slew, double& effective_capacitance);
  bool calcPi(double& gate_delay, double& driver_slew, double& effective_capacitance);
  bool initPi();
  bool calcZeroC2(double& gate_delay, double& driver_slew, double& effective_capacitance);
  bool initZeroC2();
  bool findDriverParams(double effective_capacitance, bool numerical_jacobian = false);
  bool getGateDelays(double effective_capacitance, double& threshold_delay, double& lower_delay, double& measured_slew);
  bool newtonRaphson();
  bool dampedPiNewton();
  bool evalDmpEqns();
  bool evalPiEqns();
  double calcPiCurrentDifference(double transition_time, double effective_capacitance_time, double effective_capacitance);
  bool evalOnePoleEqns();
  void calcCapacitiveWaveform(double time, double start_time, double transition_time, double capacitance, double& voltage);
  double calcCapacitiveUnitRamp(double time, double capacitance);
  void calcCapacitiveWaveformDerivative(double time, double start_time, double transition_time, double capacitance, double& start_derivative,
                                        double& transition_derivative, double& capacitance_derivative);
  double calcCapacitiveUnitRampTimeDerivative(double time, double capacitance);
  double calcCapacitiveUnitRampCapDerivative(double time, double capacitance);
  bool decomposeJacobian();
  void solveJacobian();
  bool findDriverDelaySlew(double& driver_delay, double& driver_slew);
  bool findOutputCrossing(double threshold, double lower_time, double upper_time, double& crossing_time);
  bool findRoot(std::function<void(double, double&, double&)>& function, double lower_time, double upper_time, double& root);
  void calcOutputWaveform(double time, double& voltage, double& derivative);
  void calcPiUnitRamp(double time, double& voltage, double& derivative);
  void calcZeroC2UnitRamp(double time, double& voltage, double& derivative);
  double getOutputCrossingUpperBound();
  double calcDmpExp(double value);

  static constexpr int32_t kStartTimeIndex = 0;
  static constexpr int32_t kTransitionTimeIndex = 1;
  static constexpr int32_t kEffectiveCapacitanceIndex = 2;
  static constexpr int32_t kLowerVoltageIndex = 0;
  static constexpr int32_t kThresholdVoltageIndex = 1;
  static constexpr int32_t kCurrentIndex = 2;
  static constexpr int32_t kMaxNewtonOrder = 3;
  static constexpr int32_t kMaxNewtonIteration = 100;
  static constexpr int32_t kMaxRootIteration = 20;
  static constexpr double kDriverParameterTolerance = 0.01;
  static constexpr double kThresholdTimeTolerance = 0.01;
  static constexpr double kTinyNumber = 1E-20;
  static constexpr double kGateResistanceCapacitanceStep = 1E-3;
  static constexpr double kCapacitiveDriverResistance = 1E-5;
  TimingArc* _timing_arc = nullptr;
  TransType _trans_type = TransType::kNone;
  double _input_slew = 0.0;
  double _driver_capacitance = 0.0;
  double _pi_resistance = 0.0;
  double _load_capacitance = 0.0;
  double _driver_resistance = 0.0;
  double _threshold = 0.5;
  double _lower_threshold = 0.3;
  double _upper_threshold = 0.7;
  double _slew_derate = 1.0;
  bool _is_pi = false;
  bool _is_zero_c2 = false;
  int32_t _newton_order = 0;
  double _start_time = 0.0;
  double _transition_time = 0.0;
  double _pi_pole1 = 0.0;
  double _pi_pole2 = 0.0;
  double _pi_zero = 0.0;
  double _pi_scale = 0.0;
  double _pi_constant1 = 0.0;
  double _pi_constant2 = 0.0;
  double _pi_residue1 = 0.0;
  double _pi_residue2 = 0.0;
  double _pi_current_constant = 0.0;
  double _pi_current_residue1 = 0.0;
  double _pi_current_residue2 = 0.0;
  double _zero_pole = 0.0;
  double _zero_zero = 0.0;
  double _zero_scale = 0.0;
  double _zero_constant1 = 0.0;
  double _zero_constant2 = 0.0;
  double _zero_residue = 0.0;
  std::array<double, 3> _parameter_list = {0.0, 0.0, 0.0};
  std::array<double, 3> _function_list = {0.0, 0.0, 0.0};
  std::array<std::array<double, 3>, 3> _jacobian = {{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}};
  std::array<double, 3> _scale_list = {0.0, 0.0, 0.0};
  std::array<double, 3> _delta_list = {0.0, 0.0, 0.0};
  std::array<int32_t, 3> _index_list = {0, 0, 0};
};
}  // namespace ista
