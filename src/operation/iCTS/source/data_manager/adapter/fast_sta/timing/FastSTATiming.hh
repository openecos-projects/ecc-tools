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
 * @file FastSTATiming.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS fast STA timing propagation.
 */

#pragma once

#include "FastSTA.hh"

namespace icts {

struct FastStaContext;
struct FastStaDirtyRegion;
class FastStaChar;

class FastStaTiming
{
 public:
  FastStaTiming() = delete;

  static auto update(FastStaContext& context) -> bool;
  // Local branch uses the identical clock driver/load kernels, with measured four-state seeds.
  static auto updateBranch(FastStaContext& context, FastStaNodeId input_node, const FastStaBranchStates& states) -> bool;
  static auto updateClockTopology(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool;
  static auto updateRegion(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool;
  static auto collectAffectedLogicNodes(const FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> std::optional<std::vector<FastStaNodeId>>;
  static auto updateClockTrialRegion(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool;
  static auto separate(const FastStaContext& context, const FastStaSeparationQuery& query) -> FastStaSeparationResult;
  static auto calcSkew(const FastStaContext& context) -> FastStaSkewSummary;

 private:
  friend class FastStaChar;

  static auto prepare(FastStaContext& context) -> bool;
  // The char owner preserves topology, constraints and Liberty, and reduces
  // every changed load net before requesting slew-dependent propagation.
  static auto updatePrepared(FastStaContext& context) -> bool;
};

}  // namespace icts
