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
 * @file FastSTAClockOverlay.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Internal clock topology and routed-geometry construction contracts.
 */

#pragma once

#include <string>
#include <vector>

#include "FastSTA.hh"

namespace icts {
class Clock;
struct FastStaContext;
struct FastStaNode;

namespace fast_sta {
auto RequiresPropagationBufferModel(const FastStaNode& node) -> bool;
auto AppendClock(const Clock& clock, bool owner, FastStaContext& context, std::string& failure_reason) -> bool;
auto CollectClocks(const FastStaBuildInput& input) -> std::vector<const Clock*>;
auto CollectRoutes(const FastStaBuildInput& input) -> std::vector<FastStaClockRouteGeometry>;
auto RoutesMatch(const std::vector<FastStaClockRouteGeometry>& lhs, const std::vector<FastStaClockRouteGeometry>& rhs) -> bool;
}  // namespace fast_sta
}  // namespace icts
