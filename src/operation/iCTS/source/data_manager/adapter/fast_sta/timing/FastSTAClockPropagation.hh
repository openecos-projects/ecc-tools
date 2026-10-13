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
 * @file FastSTAClockPropagation.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Clock-domain propagation, source seeding and regional reset.
 */

#pragma once

#include <queue>

#include "FastSTA.hh"

namespace icts {
struct FastStaContext;
struct FastStaDirtyRegion;
namespace fast_sta {
auto ResetTiming(FastStaContext& context) -> void;
auto ResetTiming(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> void;
auto PrepareRegionalClockTiming(FastStaContext& context, const FastStaDirtyRegion& dirty_region, std::queue<FastStaNodeId>& ready) -> bool;
auto HasCompleteSinkTiming(const FastStaContext& context) -> bool;
auto HasTimingPropagationState(const FastStaContext& context) -> bool;
auto PropagateReadyQueue(FastStaContext& context, std::queue<FastStaNodeId>& ready_nodes, bool clock_only = false) -> void;
auto SeedClockSources(FastStaContext& context) -> void;
}  // namespace fast_sta
}  // namespace icts
