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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "Arc.hpp"

namespace {

double getDelay(ista::Arc& arc, int32_t timing_arc_idx, ista::AnalysisType analysis_type)
{
  return arc.get_timing_arc_delay_map()[timing_arc_idx][analysis_type][ista::TransType::kRise][ista::TransType::kFall];
}

bool testMinDelayReplacesInitializationFallback()
{
  ista::Arc arc;
  constexpr int32_t kTimingArcIndex = 3;

  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.010, true);
  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.008, true);
  if (getDelay(arc, kTimingArcIndex, ista::AnalysisType::kMin) != 0.008) {
    return false;
  }

  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.020, false);
  if (getDelay(arc, kTimingArcIndex, ista::AnalysisType::kMin) != 0.020) {
    return false;
  }

  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.018, false);
  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.025, false);
  return getDelay(arc, kTimingArcIndex, ista::AnalysisType::kMin) == 0.018;
}

bool testMaxDelayReplacesInitializationFallback()
{
  ista::Arc arc;
  constexpr int32_t kTimingArcIndex = 7;

  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMax, ista::TransType::kRise, ista::TransType::kFall, 0.010, true);
  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMax, ista::TransType::kRise, ista::TransType::kFall, 0.012, true);
  if (getDelay(arc, kTimingArcIndex, ista::AnalysisType::kMax) != 0.012) {
    return false;
  }

  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMax, ista::TransType::kRise, ista::TransType::kFall, 0.005, false);
  if (getDelay(arc, kTimingArcIndex, ista::AnalysisType::kMax) != 0.005) {
    return false;
  }

  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMax, ista::TransType::kRise, ista::TransType::kFall, 0.007, false);
  arc.update_timing_arc_delay(kTimingArcIndex, ista::AnalysisType::kMax, ista::TransType::kRise, ista::TransType::kFall, 0.004, false);
  return getDelay(arc, kTimingArcIndex, ista::AnalysisType::kMax) == 0.007;
}

bool testFallbackKeysAreIndependent()
{
  ista::Arc arc;
  constexpr int32_t kFirstTimingArcIndex = 11;
  constexpr int32_t kSecondTimingArcIndex = 12;

  arc.update_timing_arc_delay(kFirstTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.010, true);
  arc.update_timing_arc_delay(kSecondTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.020, true);
  arc.update_timing_arc_delay(kFirstTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.030, false);
  arc.update_timing_arc_delay(kSecondTimingArcIndex, ista::AnalysisType::kMin, ista::TransType::kRise, ista::TransType::kFall, 0.040, false);

  if (getDelay(arc, kFirstTimingArcIndex, ista::AnalysisType::kMin) != 0.030) {
    return false;
  }
  return getDelay(arc, kSecondTimingArcIndex, ista::AnalysisType::kMin) == 0.040;
}

}  // namespace

int main()
{
  return testMinDelayReplacesInitializationFallback() && testMaxDelayReplacesInitializationFallback() && testFallbackKeysAreIndependent() ? 0 : 1;
}
