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

#include <cstddef>
#include <optional>
#include <vector>

#include "ParasiticArnoldiModel.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/TimingArc.hpp"

namespace ista {

// Fractions of the voltage swing, resolved from the source/receiver library.
struct WaveformThresholds
{
  double delay;
  double lower;
  double upper;
  double derate;
};

// Indexed in model terminal order; node and pin names belong to the adapter.
struct InputPortDelayResult
{
  std::vector<double> delays;
  std::vector<double> slews;
};

// Reduced RC waveform operations. Every solve uses only caller-owned inputs and local scratch.
class ArnoldiWaveformSolver
{
 public:
  // Prescribed voltage source: nonnegative finite slew, no drive resistance.
  // Zero time constants retain their ideal-ramp contribution.
  static std::optional<InputPortDelayResult> inputPortResponse(const ParasiticArnoldiModel& model, TransType transition, double input_slew,
                                                               const WaveformThresholds& source, const std::vector<WaveformThresholds>& loads);
  static ParasiticArnoldiPoleResidue poleResidue(const ParasiticArnoldiModel& arnoldi_model, double drive_resistance);
  // Minimum nonnegative ramp matching the RC threshold span. Invalid or
  // infeasible inputs yield NaN; the step-response span selects zero width.
  static void solveRamp(double pole, double transition_time, double lower_threshold, double upper_threshold, double& ramp);
  static double rampTime(double pole, double ramp, double voltage);
  // Partial derivative of threshold time with respect to R*C, with ramp fixed.
  static double rampTimeSensitivity(double pole, double ramp, double voltage);
  // Lumped capacitance reproducing a distributed source threshold crossing.
  static std::optional<double> crossingCapacitance(double driver_ramp, double drive_resistance, double total_capacitance,
                                                   double crossing_time, double voltage);
  static double effectiveCapacitance(double driver_ramp, double drive_resistance, const ParasiticArnoldiPoleResidue& pole_residue,
                                     double effective_capacitance_time);
  static void crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double voltage, double& waveform_time);
  static void crossingTime(double driver_ramp, const ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double upper_threshold,
                           double& upper_time, double mid_threshold, double& mid_time, double lower_threshold, double& lower_time);
  static double inputPortCrossing(const std::vector<double>& time_constants, const std::vector<double>& weights, double ramp, double voltage);

 private:
  static bool solveTridiagonal(const std::vector<double>& diagonal_list, const std::vector<double>& off_diagonal_list, std::vector<double>& eigenvalue_list,
                               std::vector<std::vector<double>>& eigenvector_list);
  static double hInverse(double value);
  static void rampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative);
  static double voltageAt(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list);
  static void voltageDerivative(double time, double driver_ramp, const std::vector<double>& pole_list, const std::vector<double>& residue_list, double& voltage,
                                double& derivative);
};

}  // namespace ista
