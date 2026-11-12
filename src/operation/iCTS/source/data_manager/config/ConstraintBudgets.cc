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
 * @file ConstraintBudgets.cc
 * @brief Native design-rule results override JSON independently of tightness.
 */
#include "ConstraintBudgets.hh"

#include <cmath>
#include <optional>
#include <utility>

#include "Config.hh"
#include "timing/TimingConstraints.hh"

namespace icts {
namespace {

auto designLimit(const std::vector<SdcElectricalLimit>& limits, SdcElectricalRule rule) -> std::optional<double>
{
  for (const auto& limit : limits) {
    if (limit.scope == SdcElectricalScope::kDesign && limit.rule == rule) {
      return limit.value;
    }
  }
  return std::nullopt;
}

auto acceptLimit(const std::optional<double>& value, ConstraintBudget& row) -> bool
{
  if (!value.has_value()) {
    return false;
  }
  if (!std::isfinite(*value) || *value <= 0.0) {
    row.rejected = true;
    return false;
  }
  row.value = *value;
  row.from_sdc = true;
  return true;
}

}  // namespace

auto ApplyConstraintBudgets(const std::vector<SdcElectricalLimit>& limits, Config& config) -> std::vector<ConstraintBudget>
{
  ConstraintBudget fanout{.rule = "max_fanout", .value = static_cast<double>(config.get_max_fanout()), .configured = config.has_max_fanout()};
  const auto fanout_limit = designLimit(limits, SdcElectricalRule::kFanout);
  if (fanout_limit.has_value()) {
    if (std::isfinite(*fanout_limit) && *fanout_limit >= Config::kMinFanout && *fanout_limit <= Config::kMaxFanout) {
      config.set_max_fanout(static_cast<unsigned>(std::floor(*fanout_limit)));
      fanout.value = config.get_max_fanout();
      fanout.from_sdc = true;
    } else {
      fanout.rejected = true;
    }
  }
  ConstraintBudget transition{.rule = "max_transition", .value = config.get_max_buf_tran(), .configured = config.has_max_buf_tran()};
  if (acceptLimit(designLimit(limits, SdcElectricalRule::kTransition), transition)) {
    config.set_max_buf_tran(transition.value);
    config.set_max_sink_tran(transition.value);
    config.set_sdc_max_transition_ns(transition.value);
  }
  ConstraintBudget cap{.rule = "max_capacitance", .value = config.get_max_cap(), .configured = config.has_max_cap()};
  if (acceptLimit(designLimit(limits, SdcElectricalRule::kCapacitance), cap)) {
    config.set_max_cap(cap.value);
    config.set_sdc_max_cap_pf(cap.value);
  }
  return {std::move(fanout), std::move(transition), std::move(cap)};
}

}  // namespace icts
