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

#include "DelayResult.hpp"
#include "ParasiticDmpModel.hpp"
#include "liberty/TimingArc.hpp"

namespace ista {

// Local driver/load solve. Receiver thresholds and source/pin aliases are applied by the caller.
class DmpDelayCalculator
{
 public:
  DriverResponse calculate(TimingArc& timing_arc, TransType output_trans_type, double input_slew, const ParasiticDmpModel& dmp_model, double output_load) const;
  // Receiver thresholds are fractions of the source voltage swing.
  static bool receiverResponse(const ParasiticDmpLoadModel& model, const TimingArc& source, TransType edge, double driver_slew, double threshold, double lower,
                               double upper, double derate, double& delay, double& slew);

 private:
  static bool calculateLoad(const ParasiticDmpLoadModel& load_model, TimingArc& timing_arc, TransType trans_type, double driver_slew, double& wire_delay,
                            double& load_slew);
  static double loadTime(double threshold, double pole1, double pole2, double residue_pole1, double residue_pole2, double transition_time);
};

}  // namespace ista
