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

#include <optional>
#include <vector>

#include "ParasiticArnoldiModel.hpp"
#include "liberty/TimingArc.hpp"

namespace ista {

// Terminal values use the reduced model order; receiver-library correction belongs to the caller.
struct ArnoldiDelayResult
{
  double effective_capacitance;
  double drive_resistance;
  std::vector<double> delays;
  std::vector<double> slews;
  double driver_ramp;
  ParasiticArnoldiPoleResidue waveform;
};

class ArnoldiDelayCalculator
{
 public:
  std::optional<ArnoldiDelayResult> calculate(TimingArc& timing_arc, TransType output_trans_type, double input_slew,
                                             const ParasiticArnoldiModel& arnoldi_model) const;

 private:
  static double tableDelay(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  static double tableSlew(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  static double tableSensitivity(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
};

}  // namespace ista
