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
 * @file ConstraintBudgets.hh
 * @brief Consume already-bound constraint values as CTS synthesis budgets.
 */
#pragma once

#include <string>
#include <vector>

namespace icts {
class Config;
struct SdcElectricalLimit;

struct ConstraintBudget
{
  std::string rule;
  double value = 0.0;
  bool from_sdc = false;
  bool configured = false;
  bool rejected = false;
};

// Only native design-scoped facts can replace a single global budget. Local
// facts remain in the owned snapshot and are never widened to the whole design.
auto ApplyConstraintBudgets(const std::vector<SdcElectricalLimit>& limits, Config& config) -> std::vector<ConstraintBudget>;

}  // namespace icts
