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
 * @file FastSTALogicAnalysis.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Prepared logic traversal, four-state results and propagation contracts.
 */

#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "clock_state/FastSTAClockState.hh"
#include "timing/FastSTADmpCeff.hh"

namespace icts {
struct FastStaDirtyRegion;
namespace fast_sta {
using LogicEdge = FastStaLogicEdge;
using LogicTraversal = FastStaLogicTraversal;

struct LogicAnalysis
{
  std::vector<std::array<FastStaTimingPoint, 2U>> early;
  std::vector<std::array<FastStaTimingPoint, 2U>> late;
  std::vector<FastStaTimingRelationFact> relations;
  bool tagged = false;
  std::vector<std::array<std::vector<FastStaTimingPoint>, 2U>> tagged_early;
  std::vector<std::array<std::vector<FastStaTimingPoint>, 2U>> tagged_late;
  std::size_t conditional_fallback_arc_count = 0U;
  std::size_t conditional_extrema_bundle_count = 0U;
  double propagation_runtime_s = 0.0;
  double relation_extraction_runtime_s = 0.0;
  std::string diagnostic;
  const FastStaContext* baseline = nullptr;
  std::vector<std::size_t> local_index;
  std::size_t updated_logic_node_count = 0U;
  std::size_t visited_logic_node_count = 0U;

  auto initialize(const FastStaContext& context, bool retain_tags, const std::vector<bool>* affected = nullptr) -> void
  {
    baseline = &context;
    tagged = retain_tags;
    std::size_t count = context.nodes.size();
    if (affected != nullptr) {
      local_index.assign(context.nodes.size(), std::numeric_limits<std::size_t>::max());
      count = 0U;
      for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
        if (affected->at(node_id) || context.nodes.at(node_id).domain == FastStaNodeDomain::kClock) {
          local_index.at(node_id) = count++;
        }
      }
    }
    early.resize(count);
    late.resize(count);
    if (tagged) {
      tagged_early.resize(count);
      tagged_late.resize(count);
    }
    for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
      updated_logic_node_count += includes(node_id) && context.nodes.at(node_id).domain == FastStaNodeDomain::kLogic ? 1U : 0U;
    }
  }

  [[nodiscard]] auto includes(FastStaNodeId node_id) const -> bool
  {
    return local_index.empty() || local_index.at(node_id) != std::numeric_limits<std::size_t>::max();
  }

  [[nodiscard]] auto index(FastStaNodeId node_id) const -> std::size_t { return local_index.empty() ? node_id : local_index.at(node_id); }

  [[nodiscard]] auto states(FastStaNodeId node_id, bool is_early) const -> const std::array<FastStaTimingPoint, 2U>&
  {
    if (!includes(node_id)) {
      return is_early ? baseline->nodes.at(node_id).early_timing : baseline->nodes.at(node_id).late_timing;
    }
    return is_early ? early.at(index(node_id)) : late.at(index(node_id));
  }

  auto mutableStates(FastStaNodeId node_id, bool is_early) -> std::array<FastStaTimingPoint, 2U>&
  {
    return is_early ? early.at(index(node_id)) : late.at(index(node_id));
  }

  [[nodiscard]] auto tagStates(FastStaNodeId node_id, bool is_early) const -> const std::array<std::vector<FastStaTimingPoint>, 2U>&
  {
    if (!includes(node_id)) {
      return is_early ? baseline->nodes.at(node_id).tagged_early_timing : baseline->nodes.at(node_id).tagged_late_timing;
    }
    return is_early ? tagged_early.at(index(node_id)) : tagged_late.at(index(node_id));
  }

  auto mutableTagStates(FastStaNodeId node_id, bool is_early) -> std::array<std::vector<FastStaTimingPoint>, 2U>&
  {
    return is_early ? tagged_early.at(index(node_id)) : tagged_late.at(index(node_id));
  }

  [[nodiscard]] auto complete() const -> bool { return diagnostic.empty(); }
};

struct CellResponse
{
  std::size_t arc_variant_index = 0U;
  FastStaTransition input_transition = FastStaTransition::kRise;
  FastStaTransition output_transition = FastStaTransition::kRise;
  std::size_t analysis_index = 0U;
  double gate_delay_ns = 0.0;
  double driver_slew_ns = 0.0;
  bool valid = false;
};

struct LogicResponseCache
{
  std::vector<std::vector<CellResponse>> cell_responses;
  std::vector<std::array<std::array<std::optional<FastStaDmpDriverResult>, 2U>, 2U>> net_drivers;
  std::vector<std::array<std::array<std::vector<FastStaDmpLoadResult>, 2U>, 2U>> net_loads;
};

auto LogicNetLoad(const FastStaContext& context, const FastStaNet& net) -> double;
auto ProposedDelta(const std::unordered_map<FastStaNodeId, double>& deltas, FastStaNodeId node_id) -> double;
auto TimingSources(const LogicAnalysis& analysis, FastStaNodeId node, std::size_t transition, bool early) -> std::span<const FastStaTimingPoint>;
auto MakeLogicPreparation(const FastStaContext& context) -> std::shared_ptr<const FastStaLogicPreparation>;
auto SeedLogicTiming(const FastStaContext& context, const std::unordered_map<FastStaNodeId, double>& clock_deltas, LogicAnalysis& result) -> bool;
auto SeedLogicRootSlew(const FastStaContext& context, const std::vector<std::size_t>& indegree, LogicAnalysis& result) -> void;
auto PropagateLogicTiming(const FastStaContext& context, const LogicTraversal& traversal, LogicAnalysis& result, LogicResponseCache* response_cache = nullptr,
                          const LogicAnalysis* slew_analysis = nullptr) -> bool;
auto AnalyzeLogic(const FastStaContext& context, const std::unordered_map<FastStaNodeId, double>& clock_deltas) -> LogicAnalysis;
auto AffectedLogicNodes(const FastStaContext& context, const FastStaDirtyRegion& dirty_region, const std::vector<std::vector<LogicEdge>>& outgoing)
    -> std::vector<bool>;
auto AnalyzeLogicRegion(const FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> LogicAnalysis;
auto FillLogicSummary(const FastStaContext& context, const LogicAnalysis& analysis, FastStaTimingSummary& summary) -> void;
auto PublishLogicAnalysis(FastStaContext& context, LogicAnalysis analysis) -> void;
}  // namespace fast_sta
}  // namespace icts
