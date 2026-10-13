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
 * @file FastSTABuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Initialization bridge from committed CTS state to a fast STA context.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "clock_state/FastSTAClockState.hh"

namespace icts {

class Net;
struct FastStaBuildInput;
struct FastStaEnvironment;
struct WrapperTimingGraph;
template <typename T>
class ClockSteinerTree;

class FastStaBuilder
{
 public:
  FastStaBuilder() = delete;

  struct BuildResult
  {
    std::optional<FastStaContext> context = std::nullopt;
    std::string failure_reason = "";

    auto ok() const -> bool { return context.has_value(); }
  };

  static auto buildContext(const FastStaEnvironment& environment, const FastStaBuildInput& input, const FastStaContext* prepared_context = nullptr)
      -> BuildResult;
  static auto spliceClockContext(FastStaContext& context, FastStaContext overlay, const SdcClockData& constraints) -> std::optional<std::string>;
  static auto extractClockContext(const FastStaContext& source, const FastStaBuildInput& input) -> BuildResult;
  static auto matchesClockInput(const FastStaContext& context, const FastStaBuildInput& input) -> bool;
  static auto injectNetRouteTree(FastStaContext& context, const Net& net, const ClockSteinerTree<int>& route_tree) -> bool;
  static auto validateTimingGraphJoins(const WrapperTimingGraph& graph, const FastStaContext& context) -> std::optional<std::string>;

 private:
  struct MatchingClockOverlay
  {
    FastStaContext context;
    std::vector<FastStaNodeId> source_node_ids;
    std::vector<FastStaNetId> source_net_ids;
  };

  static auto buildMatchingClockOverlay(const FastStaContext& context, const FastStaBuildInput& input) -> std::optional<MatchingClockOverlay>;
};

}  // namespace icts
