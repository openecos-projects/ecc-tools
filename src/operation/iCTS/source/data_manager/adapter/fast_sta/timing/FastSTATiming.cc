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
 * @file FastSTATiming.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS fast STA timing propagation implementation.
 */

#include "FastSTATiming.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "FastSTAClockPropagation.hh"
#include "FastSTAClockState.hh"
#include "FastSTAConstraints.hh"
#include "FastSTALogicAnalysis.hh"
#include "FastSTAParasitics.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"

namespace icts {

using fast_sta::AffectedLogicNodes;
using fast_sta::AnalyzeLogic;
using fast_sta::AnalyzeLogicRegion;
using fast_sta::FillLogicSummary;
using fast_sta::HasCompleteSinkTiming;
using fast_sta::HasTimingPropagationState;
using fast_sta::MakeLogicPreparation;
using fast_sta::PrepareRegionalClockTiming;
using fast_sta::PropagateReadyQueue;
using fast_sta::PublishLogicAnalysis;
using fast_sta::ResetTiming;
using fast_sta::SeedClockSources;

namespace {

auto propagateTiming(FastStaContext& context, std::queue<FastStaNodeId>& ready_nodes) -> bool
{
  const auto start = std::chrono::steady_clock::now();
  const auto clock_start = start;
  const auto unsupported_gate = std::ranges::find_if(context.timing_arcs, [&](const FastStaTimingArc& arc) -> bool {
    const auto model = context.liberty_cell_by_master.find(arc.cell_master);
    return arc.clock_gate_boundary && (model == context.liberty_cell_by_master.end() || !model->second.clock_gate.has_value());
  });
  if (unsupported_gate != context.timing_arcs.end()) {
    FastStaTimingSummary summary;
    summary.status = FastStaTimingStatus::kUnsupported;
    summary.unsupported_count = 1U;
    summary.fallback_reason = "clock_gate_control_timing_unavailable";
    summary.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    context.timing_relations.clear();
    context.timing_relation_seeds = {};
    context.timing_summary = std::move(summary);
    return false;
  }
  PropagateReadyQueue(context, ready_nodes, !HasTimingPropagationState(context));
  FastStaTimingSummary summary;
  summary.clock_propagation_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock_start).count();
  if (!HasTimingPropagationState(context)) {
    summary.status = FastStaTimingStatus::kComplete;
    summary.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    context.timing_relations.clear();
    context.timing_relation_seeds = {};
    context.timing_summary = std::move(summary);
    return true;
  }
  auto analysis = AnalyzeLogic(context, {});
  FillLogicSummary(context, analysis, summary);
  if (!analysis.complete()) {
    summary.status = FastStaTimingStatus::kUnsupported;
    summary.unsupported_count = 1U;
    summary.fallback_reason = analysis.diagnostic;
    summary.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    context.timing_summary = std::move(summary);
    return false;
  }
  PublishLogicAnalysis(context, std::move(analysis));
  summary.status = FastStaTimingStatus::kComplete;
  summary.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  context.timing_summary = std::move(summary);
  return true;
}

}  // namespace

auto FastStaTiming::collectAffectedLogicNodes(const FastStaContext& context, const FastStaDirtyRegion& dirty_region)
    -> std::optional<std::vector<FastStaNodeId>>
{
  if (!HasTimingPropagationState(context)) {
    return std::vector<FastStaNodeId>{};
  }
  const auto prepared = context.logic_preparation != nullptr ? context.logic_preparation : MakeLogicPreparation(context);
  if (!prepared->diagnostic.empty()) {
    return std::nullopt;
  }
  const auto affected = AffectedLogicNodes(context, dirty_region, prepared->outgoing);
  std::vector<FastStaNodeId> nodes;
  for (FastStaNodeId id = 0U; id < context.nodes.size(); ++id) {
    if (affected.at(id) && context.nodes.at(id).domain == FastStaNodeDomain::kLogic) {
      nodes.push_back(id);
    }
  }
  return nodes;
}

auto FastStaTiming::updateBranch(FastStaContext& context, FastStaNodeId input_node, const FastStaBranchStates& states) -> bool
{
  if (input_node >= context.nodes.size()) {
    return false;
  }
  ResetTiming(context);
  FastStaParasitics::updateNetLoads(context);
  for (FastStaNetId net_id = 0U; net_id < context.nets.size(); ++net_id) {
    if (!FastStaParasitics::reduceToPiElmore(context, net_id)) {
      return false;
    }
  }
  auto& source = context.nodes.at(input_node);
  for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
    for (std::size_t transition = 0U; transition < 2U; ++transition) {
      const auto& seed = states.at(analysis).at(transition);
      if (!seed.valid || !std::isfinite(seed.arrival_ns) || !std::isfinite(seed.slew_ns) || seed.slew_ns < 0.0) {
        return false;
      }
      auto& point = analysis == 0U ? source.early_timing.at(transition) : source.late_timing.at(transition);
      point = {
          .arrival_ns = seed.arrival_ns,
          .slew_ns = seed.slew_ns,
          .launch_node_id = input_node,
          .launch_clock_node_id = input_node,
          .launch_clock_transition = seed.source_transition,
          .valid = true,
          .clock_name = context.clock_name,
      };
    }
  }
  source.timing = source.late_timing.front();
  std::queue<FastStaNodeId> ready;
  ready.push(input_node);
  PropagateReadyQueue(context, ready);
  context.timing_valid = HasCompleteSinkTiming(context);
  context.clock_timing_valid = context.timing_valid;
  return context.timing_valid;
}

auto FastStaTiming::update(FastStaContext& context) -> bool
{
  return prepare(context) && updatePrepared(context);
}

auto FastStaTiming::prepare(FastStaContext& context) -> bool
{
  if (const auto failure = FastStaConstraints::prepare(context); failure.has_value()) {
    context.timing_valid = false;
    context.clock_timing_valid = false;
    context.timing_relations.clear();
    context.timing_summary = {.status = failure->starts_with("unsupported_") ? FastStaTimingStatus::kUnsupported : FastStaTimingStatus::kInvalidInput,
                              .fallback_reason = *failure};
    return false;
  }
  FastStaParasitics::updateNetLoads(context);
  for (FastStaNetId net_id = 0U; net_id < context.nets.size(); ++net_id) {
    if (!FastStaParasitics::reduceToPiElmore(context, net_id)) {
      ResetTiming(context);
      context.timing_valid = false;
      context.clock_timing_valid = false;
      context.timing_summary.status = FastStaTimingStatus::kInvalidInput;
      return false;
    }
  }
  context.logic_preparation = MakeLogicPreparation(context);
  if (!context.logic_preparation->diagnostic.empty()) {
    context.timing_valid = false;
    context.clock_timing_valid = false;
    context.timing_summary = {.status = FastStaTimingStatus::kInvalidInput, .fallback_reason = context.logic_preparation->diagnostic};
    return false;
  }
  return true;
}

auto FastStaTiming::updatePrepared(FastStaContext& context) -> bool
{
  ResetTiming(context);
  SeedClockSources(context);

  std::queue<FastStaNodeId> ready_nodes;
  for (const auto source_id : context.clock_source_node_ids) {
    ready_nodes.push(source_id);
  }

  const auto unified_timing_valid = propagateTiming(context, ready_nodes);
  context.skew = calcSkew(context);
  context.timing_valid = HasCompleteSinkTiming(context) && unified_timing_valid;
  context.clock_timing_valid = context.timing_valid;
  context.logic_tags_valid = context.timing_valid;
  context.timing_summary.updated_clock_node_count
      = static_cast<std::size_t>(std::ranges::count_if(context.nodes, [](const auto& node) -> bool { return node.domain == FastStaNodeDomain::kClock; }));
  context.timing_summary.updated_clock_net_count
      = static_cast<std::size_t>(std::ranges::count_if(context.nets, [](const auto& net) -> bool { return net.domain == FastStaNetDomain::kClock; }));
  context.timing_summary.max_skew_ns = context.skew.valid ? context.skew.skew_ns : 0.0;
  return context.timing_valid;
}

auto FastStaTiming::updateClockTopology(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool
{
  if (!dirty_region.valid) {
    return false;
  }
  const auto start = std::chrono::steady_clock::now();
  context.timing_valid = false;
  context.clock_timing_valid = false;
  context.power_valid = false;
  context.logic_preparation = MakeLogicPreparation(context);
  if (!context.logic_preparation->diagnostic.empty()) {
    context.timing_summary = {.status = FastStaTimingStatus::kInvalidInput, .fallback_reason = context.logic_preparation->diagnostic};
    return false;
  }
  FastStaParasitics::updateNetLoads(context, dirty_region.net_ids);
  for (const auto id : dirty_region.net_ids) {
    if (!FastStaParasitics::reduceToPiElmore(context, id)) {
      context.timing_summary = {.status = FastStaTimingStatus::kInvalidInput, .fallback_reason = "clock_topology_rc_reduction_failed"};
      return false;
    }
  }
  ResetTiming(context, dirty_region);
  SeedClockSources(context);
  std::queue<FastStaNodeId> ready;
  for (const auto source : context.clock_source_node_ids) {
    ready.push(source);
  }
  PropagateReadyQueue(context, ready, !HasTimingPropagationState(context));
  FastStaTimingSummary summary;
  summary.clock_propagation_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  summary.updated_clock_node_count = dirty_region.node_ids.size();
  summary.updated_clock_net_count = dirty_region.net_ids.size();
  if (HasTimingPropagationState(context)) {
    auto analysis = AnalyzeLogicRegion(context, dirty_region);
    if (!analysis.complete()) {
      context.timing_summary = {.status = FastStaTimingStatus::kUnsupported, .fallback_reason = analysis.diagnostic};
      return false;
    }
    FillLogicSummary(context, analysis, summary);
    PublishLogicAnalysis(context, std::move(analysis));
  } else {
    context.timing_relations.clear();
    context.timing_relation_seeds = {};
    context.logic_tags_valid = true;
  }
  context.skew = calcSkew(context);
  context.timing_valid = HasCompleteSinkTiming(context);
  context.clock_timing_valid = context.timing_valid;
  summary.status = context.timing_valid ? FastStaTimingStatus::kComplete : FastStaTimingStatus::kInvalidInput;
  summary.max_skew_ns = context.skew.valid ? context.skew.skew_ns : 0.0;
  summary.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  context.timing_summary = std::move(summary);
  return context.timing_valid;
}

auto FastStaTiming::updateRegion(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool
{
  if (!dirty_region.valid || dirty_region.start_node_id >= context.nodes.size()) {
    return false;
  }
  const auto full_rebuild = [&](std::string reason) -> bool {
    const bool rebuilt = update(context);
    context.timing_summary.used_full_rebuild = true;
    if (rebuilt || context.timing_summary.fallback_reason.empty()) {
      context.timing_summary.fallback_reason = std::move(reason);
    }
    return rebuilt;
  };
  if (!context.timing_valid) {
    return full_rebuild("incremental_fallback:timing_state_invalid:v1");
  }
  if ((!context.constraints.clocks.empty()) && !context.logic_tags_valid && HasTimingPropagationState(context)) {
    return full_rebuild("incremental_fallback:logic_tags_unavailable:v1");
  }

  const auto& load_update_net_ids = dirty_region.load_update_net_ids.empty() ? dirty_region.net_ids : dirty_region.load_update_net_ids;
  FastStaParasitics::updateNetLoads(context, load_update_net_ids);
  for (const auto net_id : load_update_net_ids) {
    if (!FastStaParasitics::reduceToPiElmore(context, net_id)) {
      context.timing_valid = false;
      context.clock_timing_valid = false;
      context.timing_summary.status = FastStaTimingStatus::kInvalidInput;
      return false;
    }
  }

  std::queue<FastStaNodeId> ready_nodes;
  if (!PrepareRegionalClockTiming(context, dirty_region, ready_nodes)) {
    context.timing_valid = false;
    context.clock_timing_valid = false;
    context.timing_summary = {.status = FastStaTimingStatus::kInvalidInput, .fallback_reason = "incremental_start_timing_invalid"};
    return false;
  }

  const auto timing_start = std::chrono::steady_clock::now();
  PropagateReadyQueue(context, ready_nodes, !HasTimingPropagationState(context));
  auto summary = FastStaTimingSummary{.used_full_rebuild = false, .fallback_reason = {}};
  summary.clock_propagation_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - timing_start).count();
  summary.updated_clock_node_count = dirty_region.node_ids.size();
  summary.updated_clock_net_count = dirty_region.net_ids.size();
  bool unified_timing_valid = true;
  if (HasTimingPropagationState(context)) {
    auto analysis = AnalyzeLogicRegion(context, dirty_region);
    if (!analysis.complete()) {
      context.timing_valid = false;
      context.clock_timing_valid = false;
      summary.status = FastStaTimingStatus::kUnsupported;
      summary.fallback_reason = analysis.diagnostic;
      context.timing_summary = std::move(summary);
      return false;
    } else {
      FillLogicSummary(context, analysis, summary);
      PublishLogicAnalysis(context, std::move(analysis));
      summary.status = FastStaTimingStatus::kComplete;
    }
  } else {
    summary.status = FastStaTimingStatus::kComplete;
    context.timing_relations.clear();
    context.timing_relation_seeds = {};
  }
  summary.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - timing_start).count();
  context.timing_summary = std::move(summary);
  context.skew = calcSkew(context);
  context.timing_valid = HasCompleteSinkTiming(context) && unified_timing_valid;
  context.clock_timing_valid = context.timing_valid;
  context.timing_summary.max_skew_ns = context.skew.valid ? context.skew.skew_ns : 0.0;
  return context.timing_valid;
}

auto FastStaTiming::updateClockTrialRegion(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool
{
  if (!(context.timing_valid || context.clock_timing_valid) || !dirty_region.valid || dirty_region.start_node_id >= context.nodes.size()) {
    return false;
  }
  const auto& load_update_net_ids = dirty_region.load_update_net_ids.empty() ? dirty_region.net_ids : dirty_region.load_update_net_ids;
  FastStaParasitics::updateNetLoads(context, load_update_net_ids);
  for (const auto net_id : load_update_net_ids) {
    if (!FastStaParasitics::reduceToPiElmore(context, net_id)) {
      context.timing_valid = false;
      context.clock_timing_valid = false;
      context.timing_summary.status = FastStaTimingStatus::kInvalidInput;
      return false;
    }
  }

  std::queue<FastStaNodeId> ready_nodes;
  if (!PrepareRegionalClockTiming(context, dirty_region, ready_nodes)) {
    return false;
  }

  PropagateReadyQueue(context, ready_nodes, true);
  context.skew = calcSkew(context);
  context.clock_timing_valid = HasCompleteSinkTiming(context);
  context.timing_valid = false;
  context.logic_tags_valid = false;
  context.power_valid = false;
  return context.clock_timing_valid;
}

}  // namespace icts
