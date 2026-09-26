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
 * @file OptimizationState.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Fast STA state capture and improvement checks for CTS optimization.
 */

#include "optimization/state/OptimizationState.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include "FastSTA.hh"
#include "optimization/model/ClockSizingOptimizationData.hh"

namespace icts::clock_sizing_optimization {

namespace {

auto CheckCapLegality(const FastSTA& fast_sta, FastStaContextId context_id, const std::vector<ClockSizingCapLimit>& baseline) -> ClockSizingCapCheck
{
  ClockSizingCapCheck result;
  const auto scope = fast_sta.queryClockElectricalScope(context_id);
  if (!scope.has_value() || scope->net_ids.size() != baseline.size()) {
    result.legal = false;
    return result;
  }
  for (std::size_t index = 0U; index < baseline.size(); ++index) {
    const auto& limit = baseline.at(index);
    const auto net_id = scope->net_ids.at(index);
    if (limit.net_id != net_id) {
      result.legal = false;
      ++result.violation_count;
      continue;
    }
    const auto cap_status = fast_sta.queryCapStatus(context_id, net_id);
    if (!cap_status.has_value()) {
      result.legal = false;
      ++result.violation_count;
      continue;
    }
    if (cap_status->max_cap_pf <= 0.0) {
      continue;
    }
    const auto baseline_load = limit.load_cap_pf;
    const auto baseline_violated = limit.violated;
    const bool legal = baseline_violated ? cap_status->load_cap_pf <= baseline_load + kClockSizingEpsilon : !cap_status->violated;
    if (!legal) {
      result.legal = false;
      ++result.violation_count;
    }
  }
  return result;
}

auto CountSlewViolationRole(ClockSizingSlewCheck& result, FastStaSlewRole role) -> void
{
  ++result.violation_count;
  switch (role) {
    case FastStaSlewRole::kBufferInput:
      ++result.buffer_violation_count;
      break;
    case FastStaSlewRole::kSink:
      ++result.sink_violation_count;
      break;
    case FastStaSlewRole::kUnknown:
      break;
  }
}

auto CheckSlewLegality(const FastSTA& fast_sta, FastStaContextId context_id, const std::vector<ClockSizingSlewLimit>& baseline) -> ClockSizingSlewCheck
{
  ClockSizingSlewCheck result;
  const auto scope = fast_sta.queryClockElectricalScope(context_id);
  if (!scope.has_value() || scope->node_ids.size() != baseline.size()) {
    result.legal = false;
    return result;
  }
  for (std::size_t index = 0U; index < baseline.size(); ++index) {
    const auto& limit = baseline.at(index);
    const auto node_id = scope->node_ids.at(index);
    if (limit.node_id != node_id) {
      result.legal = false;
      ++result.unavailable_count;
      CountSlewViolationRole(result, limit.role);
      continue;
    }
    const auto slew_status = fast_sta.querySlewStatus(context_id, node_id);
    const auto baseline_available = limit.available;
    auto max_slew_ns = 0.0;
    if (slew_status.has_value()) {
      max_slew_ns = slew_status->max_slew_ns;
    } else if (baseline_available) {
      max_slew_ns = limit.max_slew_ns;
    }
    if (max_slew_ns <= 0.0) {
      continue;
    }
    const auto role = slew_status.has_value() ? slew_status->role : limit.role;
    if (!slew_status.has_value()) {
      result.legal = false;
      ++result.unavailable_count;
      CountSlewViolationRole(result, role);
      continue;
    }
    const auto baseline_slew = baseline_available ? limit.slew_ns : 0.0;
    const auto baseline_violated = baseline_available && limit.violated;
    const bool legal = baseline_violated ? slew_status->slew_ns <= baseline_slew + kClockSizingEpsilon : !slew_status->violated;
    if (!legal) {
      result.legal = false;
      CountSlewViolationRole(result, role);
      const auto allowed_slew_ns = baseline_violated ? baseline_slew : max_slew_ns;
      if (!result.worst_violation.has_value() || slew_status->slew_ns - allowed_slew_ns > result.worst_violation->slew_ns - result.worst_allowed_slew_ns) {
        result.worst_violation = slew_status;
        result.worst_baseline_slew_ns = baseline_slew;
        result.worst_allowed_slew_ns = allowed_slew_ns;
      }
    }
  }
  return result;
}

}  // namespace

auto FirstClockSizingEditBufferIndex(const std::vector<ClockSizingEdit>& edits) -> std::size_t
{
  auto first_index = std::numeric_limits<std::size_t>::max();
  for (const auto& edit : edits) {
    first_index = std::min(first_index, edit.buffer_index);
  }
  return first_index;
}

auto CaptureState(const FastSTA& fast_sta, FastStaContextId context_id, const std::vector<ClockSizingCapLimit>& cap_baseline,
                  const std::vector<ClockSizingSlewLimit>& slew_baseline) -> ClockSizingTimingState
{
  ClockSizingTimingState state;
  state.skew = fast_sta.querySkew(context_id);
  const auto power = fast_sta.queryPower(context_id);
  if (power.has_value()) {
    state.power = *power;
  }
  state.cap = CheckCapLegality(fast_sta, context_id, cap_baseline);
  state.slew = CheckSlewLegality(fast_sta, context_id, slew_baseline);
  const auto analysis_status = fast_sta.queryAnalysisStatus(context_id);
  const bool timing_available = analysis_status.has_value() && (analysis_status->timing_valid || analysis_status->clock_timing_valid);
  state.valid = timing_available && state.skew.valid && power.has_value() && state.cap.legal && state.slew.legal;
  return state;
}

auto CaptureStateWithArea(const FastSTA& fast_sta, FastStaContextId context_id, const std::vector<ClockSizingCapLimit>& cap_baseline,
                          const std::vector<ClockSizingSlewLimit>& slew_baseline, double area_um2) -> ClockSizingTimingState
{
  ClockSizingTimingState state;
  state.skew = fast_sta.querySkew(context_id);
  state.cap = CheckCapLegality(fast_sta, context_id, cap_baseline);
  state.slew = CheckSlewLegality(fast_sta, context_id, slew_baseline);
  state.power.area_um2 = area_um2;
  const auto analysis_status = fast_sta.queryAnalysisStatus(context_id);
  state.valid = analysis_status.has_value() && analysis_status->clock_timing_valid && state.skew.valid && state.cap.legal && state.slew.legal;
  return state;
}

auto TargetMet(const ClockSizingTimingState& state, double target_skew_ns) -> bool
{
  return state.valid && state.skew.skew_ns <= target_skew_ns + kClockSizingEpsilon;
}

auto StateImproves(const ClockSizingTimingState& current, const ClockSizingTimingState& candidate, double target_skew_ns) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  const bool current_met = TargetMet(current, target_skew_ns);
  const bool candidate_met = TargetMet(candidate, target_skew_ns);
  if (current_met) {
    if (!candidate_met) {
      return false;
    }
    if (candidate.power.area_um2 < current.power.area_um2 - kClockSizingEpsilon) {
      return true;
    }
    return std::abs(candidate.power.area_um2 - current.power.area_um2) <= kClockSizingEpsilon
           && candidate.skew.skew_ns < current.skew.skew_ns - kClockSizingEpsilon;
  }
  if (candidate_met) {
    return true;
  }
  return candidate.skew.skew_ns < current.skew.skew_ns - kClockSizingEpsilon;
}

}  // namespace icts::clock_sizing_optimization
