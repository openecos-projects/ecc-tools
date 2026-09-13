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
 * @file FastSTALogicAnalysis.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Build logic traversal and publish signed timing relations and summaries.
 */

#include "FastSTALogicAnalysis.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTAClockState.hh"
#include "FastSTAConstraints.hh"
#include "FastSTAEvents.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTATiming.hh"
#include "FastSTATimingLookup.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts {
namespace fast_sta {

namespace {

auto makeLogicOrder(const FastStaContext& context, std::vector<std::vector<LogicEdge>>& outgoing, std::vector<std::size_t>& indegree, std::string& diagnostic)
    -> std::vector<FastStaNodeId>
{
  outgoing.resize(context.nodes.size());
  indegree.assign(context.nodes.size(), 0U);
  std::size_t logic_node_count = 0U;
  for (const auto& node : context.nodes) {
    logic_node_count += node.domain == FastStaNodeDomain::kLogic ? 1U : 0U;
  }
  for (std::size_t index = 0U; index < context.timing_arcs.size(); ++index) {
    const auto& arc = context.timing_arcs.at(index);
    if (arc.from_node_id >= context.nodes.size() || arc.to_node_id >= context.nodes.size()) {
      diagnostic = "timing_arc_node_id_out_of_range";
      return {};
    }
    if (context.nodes.at(arc.to_node_id).domain == FastStaNodeDomain::kClock) {
      continue;
    }
    outgoing.at(arc.from_node_id).push_back(LogicEdge{.to_node_id = arc.to_node_id, .model_index = index, .net = false});
    ++indegree.at(arc.to_node_id);
  }
  for (std::size_t index = 0U; index < context.nets.size(); ++index) {
    const auto& net = context.nets.at(index);
    if (net.domain != FastStaNetDomain::kLogic) {
      continue;
    }
    if (net.driver_node_id >= context.nodes.size() || !net.parasitic.valid) {
      diagnostic = "logic_net_parasitic_unavailable:" + net.name;
      return {};
    }
    for (std::size_t load_index = 0U; load_index < net.load_node_ids.size(); ++load_index) {
      const auto load_id = net.load_node_ids.at(load_index);
      if (load_id >= context.nodes.size()) {
        diagnostic = "logic_net_node_id_out_of_range:" + net.name;
        return {};
      }
      outgoing.at(net.driver_node_id).push_back(LogicEdge{.to_node_id = load_id, .model_index = index, .load_index = load_index, .net = true});
      ++indegree.at(load_id);
    }
  }
  std::priority_queue<FastStaNodeId, std::vector<FastStaNodeId>, std::greater<>> ready;
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (context.nodes.at(node_id).domain == FastStaNodeDomain::kLogic && indegree.at(node_id) == 0U) {
      ready.push(node_id);
    }
  }
  auto remaining = indegree;
  std::vector<FastStaNodeId> order;
  order.reserve(logic_node_count);
  while (!ready.empty()) {
    const auto node_id = ready.top();
    ready.pop();
    order.push_back(node_id);
    for (const auto& edge : outgoing.at(node_id)) {
      if (--remaining.at(edge.to_node_id) == 0U) {
        ready.push(edge.to_node_id);
      }
    }
  }
  if (order.size() != logic_node_count) {
    diagnostic = "logic_timing_graph_contains_cycle";
    return {};
  }
  return order;
}

auto makeLogicTraversal(const FastStaContext& context, const std::vector<FastStaNodeId>& order, const std::vector<std::vector<LogicEdge>>& outgoing)
    -> LogicTraversal
{
  LogicTraversal traversal;
  traversal.incoming.resize(context.nodes.size());
  std::vector<std::size_t> node_level(context.nodes.size(), 0U);
  std::size_t maximum_level = 0U;
  for (const auto node_id : order) {
    for (const auto& edge : outgoing.at(node_id)) {
      traversal.incoming.at(edge.to_node_id).emplace_back(node_id, edge);
      node_level.at(edge.to_node_id) = std::max(node_level.at(edge.to_node_id), node_level.at(node_id) + 1U);
      maximum_level = std::max(maximum_level, node_level.at(edge.to_node_id));
    }
  }
  for (auto& node_incoming : traversal.incoming) {
    std::ranges::sort(node_incoming, [](const auto& lhs, const auto& rhs) -> bool {
      return std::tuple(lhs.first, lhs.second.net, lhs.second.model_index, lhs.second.load_index)
             < std::tuple(rhs.first, rhs.second.net, rhs.second.model_index, rhs.second.load_index);
    });
  }
  traversal.nodes_by_level.resize(maximum_level + 1U);
  for (const auto node_id : order) {
    traversal.nodes_by_level.at(node_level.at(node_id)).push_back(node_id);
  }
  return traversal;
}

}  // namespace

auto MakeLogicPreparation(const FastStaContext& context) -> std::shared_ptr<const FastStaLogicPreparation>
{
  auto prepared = std::make_shared<FastStaLogicPreparation>();
  const auto order = makeLogicOrder(context, prepared->outgoing, prepared->indegree, prepared->diagnostic);
  if (prepared->diagnostic.empty()) {
    prepared->traversal = makeLogicTraversal(context, order, prepared->outgoing);
  }
  return prepared;
}

namespace {

auto extractLogicRelations(const FastStaContext& context, const std::unordered_map<FastStaNodeId, double>& clock_deltas, LogicAnalysis& result) -> bool
{
  for (const auto& check : context.timing_checks) {
    if (check.data_node_id >= context.nodes.size() || check.clock_node_id >= context.nodes.size()) {
      result.diagnostic = "timing_check_node_out_of_range";
      return false;
    }
    const auto& capture_node = context.nodes.at(check.clock_node_id);
    if (capture_node.clock_inactive || capture_node.domain != FastStaNodeDomain::kClock || context.nodes.at(check.data_node_id).case_value.has_value()) {
      continue;
    }
    const auto setup = check.kind == FastStaTimingCheckKind::kSetup;
    const auto& capture
        = setup ? capture_node.early_timing.at(TransitionIndex(check.clock_transition)) : capture_node.late_timing.at(TransitionIndex(check.clock_transition));
    if (!capture.valid) {
      result.diagnostic = "capture_clock_state_unavailable:" + capture_node.name;
      return false;
    }
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      for (const auto& data : TimingSources(result, check.data_node_id, TransitionIndex(transition), !setup)) {
        if (!data.valid || (data.launch_clock_node_id == kInvalidFastStaNodeId && data.clock_name.empty())) {
          continue;
        }
        auto requirement = check.requirement_override_ns;
        if (!requirement.has_value() && context.wrapper != nullptr) {
          const auto measured = FastStaLiberty::queryTimingCheck(*context.wrapper, check.cell_master, check.clock_port, check.data_port, check.kind,
                                                                 check.clock_transition, transition, capture.slew_ns, data.slew_ns,
                                                                 result.conditional_fallback_arc_count, result.conditional_extrema_bundle_count,
                                                                 FastStaConstraints::caseLookup(context, context.nodes.at(check.data_node_id).inst_name));
          if (measured.status == FastStaCheckStatus::kInactive) {
            continue;
          }
          if (measured.status == FastStaCheckStatus::kMeasured) {
            requirement = measured.requirement_ns;
          }
        }
        if (!requirement.has_value()) {
          result.diagnostic = "timing_check_table_unavailable:" + check.cell_master + ":" + check.clock_port + ":" + check.data_port;
          return false;
        }
        if (auto relation = FastStaEvents::relation(context, check, transition, data, capture, *requirement, clock_deltas); relation.has_value()) {
          result.relations.push_back(std::move(*relation));
        }
      }
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (!node.top_level || !node.output || node.case_value.has_value()) {
      continue;
    }
    for (const auto& clock : context.constraints.clocks) {
      for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
        for (const auto early : {true, false}) {
          for (const auto* delay : IoDelays(context, node_id, clock.clock_name, transition, early, false)) {
            const auto capture = IoReferenceClock(context, *delay, clock.clock_name, !early);
            if (!capture.valid) {
              continue;
            }
            const FastStaTimingCheck check{.data_node_id = node_id,
                                           .clock_node_id = capture.launch_clock_node_id,
                                           .kind = early ? FastStaTimingCheckKind::kHold : FastStaTimingCheckKind::kSetup,
                                           .clock_transition = capture.launch_clock_transition};
            for (const auto& data : TimingSources(result, node_id, TransitionIndex(transition), early)) {
              if (!data.valid || (data.launch_clock_node_id == kInvalidFastStaNodeId && data.clock_name.empty())) {
                continue;
              }
              if (auto relation = FastStaEvents::relation(context, check, transition, data, capture, early ? -delay->value_ns : delay->value_ns, clock_deltas);
                  relation.has_value()) {
                relation->output_delay = true;
                result.relations.push_back(std::move(*relation));
              }
            }
          }
        }
      }
    }
  }
  std::ranges::sort(result.relations,
                    [](const auto& lhs, const auto& rhs) -> bool { return std::tie(lhs.relation_id, lhs.slack_ns) < std::tie(rhs.relation_id, rhs.slack_ns); });
  result.relations.erase(std::ranges::unique(result.relations, {}, &FastStaTimingRelationFact::relation_id).begin(), result.relations.end());
  return true;
}

}  // namespace

auto AnalyzeLogic(const FastStaContext& context, const std::unordered_map<FastStaNodeId, double>& clock_deltas) -> LogicAnalysis
{
  const auto propagation_start = std::chrono::steady_clock::now();
  LogicAnalysis slew_analysis;
  slew_analysis.initialize(context, false);
  const auto prepared = context.logic_preparation != nullptr ? context.logic_preparation : MakeLogicPreparation(context);
  slew_analysis.diagnostic = prepared->diagnostic;
  if (!slew_analysis.complete()) {
    return slew_analysis;
  }
  const auto& traversal = prepared->traversal;
  SeedLogicRootSlew(context, prepared->indegree, slew_analysis);
  LogicResponseCache response_cache;
  if (!SeedLogicTiming(context, clock_deltas, slew_analysis) || !PropagateLogicTiming(context, traversal, slew_analysis, &response_cache)) {
    return slew_analysis;
  }

  LogicAnalysis result;
  result.initialize(context, !context.constraints.clocks.empty() || !context.constraints.path_exceptions.empty());
  if (!SeedLogicTiming(context, clock_deltas, result) || !PropagateLogicTiming(context, traversal, result, &response_cache, &slew_analysis)) {
    return result;
  }
  result.visited_logic_node_count += slew_analysis.visited_logic_node_count;
  result.propagation_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - propagation_start).count();
  const auto relation_start = std::chrono::steady_clock::now();
  if (!extractLogicRelations(context, clock_deltas, result)) {
    return result;
  }
  result.relation_extraction_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - relation_start).count();
  return result;
}

auto AffectedLogicNodes(const FastStaContext& context, const FastStaDirtyRegion& dirty_region, const std::vector<std::vector<LogicEdge>>& outgoing)
    -> std::vector<bool>
{
  std::vector<bool> dirty_clock_nodes(context.nodes.size(), false);
  for (const auto node_id : dirty_region.node_ids) {
    if (node_id < context.nodes.size()) {
      dirty_clock_nodes.at(node_id) = true;
    }
  }
  std::vector<bool> affected_nodes(context.nodes.size(), false);
  std::queue<FastStaNodeId> ready;
  const auto enqueue = [&](FastStaNodeId node_id) -> void {
    if (node_id < affected_nodes.size() && !affected_nodes.at(node_id)) {
      affected_nodes.at(node_id) = true;
      ready.push(node_id);
    }
  };
  // Clock pins can directly drive data paths and gating checks. They need not
  // have a sequential launch row to be a dependency of logic propagation.
  for (const auto node_id : dirty_region.node_ids) {
    enqueue(node_id);
  }
  for (const auto& launch : context.timing_launches) {
    if (launch.clock_node_id < dirty_clock_nodes.size() && dirty_clock_nodes.at(launch.clock_node_id)) {
      enqueue(launch.output_node_id);
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (!node.top_level || !node.input || node.domain != FastStaNodeDomain::kLogic) {
      continue;
    }
    for (const auto& clock : context.constraints.clocks) {
      for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
        for (const auto early : {true, false}) {
          for (const auto* delay : IoDelays(context, node_id, clock.clock_name, transition, early, true)) {
            const auto reference = IoReferenceClock(context, *delay, clock.clock_name, early);
            if (reference.valid && reference.launch_clock_node_id < dirty_clock_nodes.size() && dirty_clock_nodes.at(reference.launch_clock_node_id)) {
              enqueue(node_id);
            }
          }
        }
      }
    }
  }
  while (!ready.empty()) {
    const auto node_id = ready.front();
    ready.pop();
    for (const auto& edge : outgoing.at(node_id)) {
      enqueue(edge.to_node_id);
    }
  }
  return affected_nodes;
}

auto AnalyzeLogicRegion(const FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> LogicAnalysis
{
  const auto propagation_start = std::chrono::steady_clock::now();
  LogicAnalysis result;
  const auto prepared = context.logic_preparation != nullptr ? context.logic_preparation : MakeLogicPreparation(context);
  result.diagnostic = prepared->diagnostic;
  if (!result.complete()) {
    return result;
  }
  const auto& traversal = prepared->traversal;
  const auto affected_nodes = AffectedLogicNodes(context, dirty_region, prepared->outgoing);
  result.initialize(context, !context.constraints.clocks.empty() || !context.constraints.path_exceptions.empty(), &affected_nodes);

  LogicAnalysis slew_analysis;
  slew_analysis.initialize(context, false, &affected_nodes);
  SeedLogicRootSlew(context, prepared->indegree, slew_analysis);
  LogicResponseCache response_cache;
  if (!SeedLogicTiming(context, {}, slew_analysis) || !PropagateLogicTiming(context, traversal, slew_analysis, &response_cache)) {
    return slew_analysis;
  }
  if (!SeedLogicTiming(context, {}, result) || !PropagateLogicTiming(context, traversal, result, &response_cache, &slew_analysis)) {
    return result;
  }
  result.visited_logic_node_count += slew_analysis.visited_logic_node_count;
  result.propagation_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - propagation_start).count();
  const auto relation_start = std::chrono::steady_clock::now();
  if (!extractLogicRelations(context, {}, result)) {
    return result;
  }
  result.relation_extraction_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - relation_start).count();
  return result;
}

namespace {

auto buildSeedSet(const std::vector<FastStaTimingRelationFact>& relations) -> FastStaTimingSeedSet
{
  FastStaTimingSeedSet seeds;
  std::unordered_map<std::string, std::size_t> limiting;
  for (std::size_t index = 0U; index < relations.size(); ++index) {
    const auto& relation = relations.at(index);
    if (relation.slack_ns < 0.0) {
      seeds.relation_indexes.push_back(index);
    }
    const auto key = std::to_string(relation.capture_node_id) + "|" + (relation.check == FastStaTimingCheckKind::kSetup ? "setup" : "hold");
    const auto iter = limiting.find(key);
    if (iter == limiting.end() || relation.slack_ns < relations.at(iter->second).slack_ns
        || (relation.slack_ns == relations.at(iter->second).slack_ns && relation.relation_id < relations.at(iter->second).relation_id)) {
      limiting[key] = index;
    }
  }
  for (const auto& [key, index] : limiting) {
    (void) key;
    seeds.relation_indexes.push_back(index);
  }
  std::ranges::sort(seeds.relation_indexes);
  seeds.relation_indexes.erase(std::ranges::unique(seeds.relation_indexes).begin(), seeds.relation_indexes.end());
  return seeds;
}

}  // namespace

auto FillLogicSummary(const FastStaContext& context, const LogicAnalysis& analysis, FastStaTimingSummary& summary) -> void
{
  summary.conditional_fallback_arc_count = analysis.conditional_fallback_arc_count;
  summary.conditional_extrema_bundle_count = analysis.conditional_extrema_bundle_count;
  summary.logic_propagation_runtime_s = analysis.propagation_runtime_s;
  summary.relation_extraction_runtime_s = analysis.relation_extraction_runtime_s;
  summary.updated_logic_node_count = analysis.updated_logic_node_count;
  summary.visited_logic_node_count = analysis.visited_logic_node_count;
  summary.relation_count = analysis.relations.size();
  summary.launch_count = context.timing_launches.size();
  std::unordered_set<FastStaNodeId> endpoints;
  auto setup_wns = std::numeric_limits<double>::infinity();
  auto hold_wns = std::numeric_limits<double>::infinity();
  std::unordered_map<std::string, double> setup_by_endpoint;
  std::unordered_map<std::string, double> hold_by_endpoint;
  for (const auto& relation : analysis.relations) {
    endpoints.insert(relation.capture_node_id);
    auto& endpoint_slacks = relation.check == FastStaTimingCheckKind::kSetup ? setup_by_endpoint : hold_by_endpoint;
    const auto* endpoint_kind = relation.output_delay ? "|output" : "|cell";
    if (relation.clock_gating) {
      endpoint_kind = "|gating";
    }
    const auto endpoint_key = std::to_string(relation.capture_node_id) + "|" + relation.capture_clock_name + endpoint_kind;
    const auto [iter, inserted] = endpoint_slacks.emplace(endpoint_key, relation.slack_ns);
    if (!inserted) {
      iter->second = std::min(iter->second, relation.slack_ns);
    }
    if (relation.check == FastStaTimingCheckKind::kSetup) {
      ++summary.setup_relation_count;
      setup_wns = std::min(setup_wns, relation.slack_ns);
    } else {
      ++summary.hold_relation_count;
      hold_wns = std::min(hold_wns, relation.slack_ns);
    }
  }
  for (const auto& [endpoint, slack] : setup_by_endpoint) {
    (void) endpoint;
    summary.setup_tns_ns += std::min(0.0, slack);
    summary.setup_violation_count += slack < 0.0 ? 1U : 0U;
  }
  for (const auto& [endpoint, slack] : hold_by_endpoint) {
    (void) endpoint;
    summary.hold_tns_ns += std::min(0.0, slack);
    summary.hold_violation_count += slack < 0.0 ? 1U : 0U;
  }
  summary.endpoint_count = endpoints.size();
  summary.setup_min_slack_ns = std::isfinite(setup_wns) ? std::optional{setup_wns} : std::nullopt;
  summary.hold_min_slack_ns = std::isfinite(hold_wns) ? std::optional{hold_wns} : std::nullopt;
  summary.setup_wns_ns = std::isfinite(setup_wns) ? setup_wns : 0.0;
  summary.hold_wns_ns = std::isfinite(hold_wns) ? hold_wns : 0.0;
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.domain != FastStaNodeDomain::kLogic) {
      continue;
    }
    ++summary.node_count;
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      const auto index = TransitionIndex(transition);
      summary.max_slew_ns
          = std::max({summary.max_slew_ns, analysis.states(node_id, true).at(index).slew_ns, analysis.states(node_id, false).at(index).slew_ns});
      if (node.max_slew_ns > 0.0
          && ((analysis.states(node_id, true).at(index).valid && analysis.states(node_id, true).at(index).slew_ns > node.max_slew_ns)
              || (analysis.states(node_id, false).at(index).valid && analysis.states(node_id, false).at(index).slew_ns > node.max_slew_ns))) {
        ++summary.slew_violation_count;
      }
    }
  }
  for (const auto& net : context.nets) {
    if (net.domain != FastStaNetDomain::kLogic) {
      continue;
    }
    ++summary.net_count;
    summary.logic_rc_node_count += net.parasitic.rc_nodes.size();
    summary.logic_rc_edge_count += net.parasitic.rc_edges.size();
    for (const auto& rc_node : net.parasitic.rc_nodes) {
      summary.logic_pin_cap_count += rc_node.pin_cap_pf > 0.0 ? rc_node.terminal_node_ids.size() : 0U;
    }
    const auto cap_pf = LogicNetLoad(context, net);
    summary.max_cap_pf = std::max(summary.max_cap_pf, cap_pf);
    summary.max_fanout = std::max(summary.max_fanout, static_cast<double>(net.load_node_ids.size()));
    summary.cap_violation_count += net.max_cap_pf > 0.0 && cap_pf > net.max_cap_pf ? 1U : 0U;
    summary.fanout_violation_count += net.max_fanout > 0.0 && static_cast<double>(net.load_node_ids.size()) > net.max_fanout ? 1U : 0U;
  }
  struct Projection
  {
    double lower_ns = -std::numeric_limits<double>::infinity();
    double upper_ns = std::numeric_limits<double>::infinity();
  };
  std::unordered_map<FastStaNodeId, Projection> projections;
  for (const auto& relation : analysis.relations) {
    auto& projection = projections[relation.capture_clock_node_id];
    if (relation.check == FastStaTimingCheckKind::kSetup) {
      projection.lower_ns = std::max(projection.lower_ns, -relation.slack_ns);
    } else {
      projection.upper_ns = std::min(projection.upper_ns, relation.slack_ns);
    }
  }
  std::vector<double> widths;
  for (const auto& [node_id, projection] : projections) {
    (void) node_id;
    if (projection.lower_ns > projection.upper_ns) {
      ++summary.window_conflict_count;
    } else if (std::isfinite(projection.lower_ns) && std::isfinite(projection.upper_ns)) {
      widths.push_back(projection.upper_ns - projection.lower_ns);
    }
  }
  if (!widths.empty()) {
    std::ranges::sort(widths);
    summary.window_width_min_ns = widths.front();
    summary.window_width_median_ns = widths.at((widths.size() - 1U) / 2U);
    summary.window_width_p95_ns = widths.at(static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(widths.size() - 1U))));
    summary.window_width_max_ns = widths.back();
  }
}

auto PublishLogicAnalysis(FastStaContext& context, LogicAnalysis analysis) -> void
{
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (context.nodes.at(node_id).domain != FastStaNodeDomain::kLogic || !analysis.includes(node_id)) {
      continue;
    }
    auto& node = context.nodes.at(node_id);
    node.early_timing = std::move(analysis.mutableStates(node_id, true));
    node.late_timing = std::move(analysis.mutableStates(node_id, false));
    if (analysis.tagged) {
      node.tagged_early_timing = std::move(analysis.mutableTagStates(node_id, true));
      node.tagged_late_timing = std::move(analysis.mutableTagStates(node_id, false));
    } else {
      node.tagged_early_timing = {};
      node.tagged_late_timing = {};
    }
  }
  context.logic_tags_valid = true;
  context.timing_relations = std::move(analysis.relations);
  context.timing_relation_seeds = buildSeedSet(context.timing_relations);
}

}  // namespace fast_sta

using fast_sta::AnalyzeLogic;

auto FastStaTiming::separate(const FastStaContext& context, const FastStaSeparationQuery& query) -> FastStaSeparationResult
{
  const auto start = std::chrono::steady_clock::now();
  FastStaSeparationResult result;
  std::unordered_map<FastStaNodeId, double> deltas;
  deltas.reserve(query.clock_deltas.size());
  for (const auto& delta : query.clock_deltas) {
    if (delta.clock_node_id >= context.nodes.size() || !std::isfinite(delta.delta_ns)
        || context.nodes.at(delta.clock_node_id).domain != FastStaNodeDomain::kClock) {
      result.diagnostic = "invalid_clock_arrival_delta";
      return result;
    }
    if (deltas.contains(delta.clock_node_id)) {
      result.diagnostic = "duplicate_clock_arrival_delta";
      return result;
    }
    deltas.emplace(delta.clock_node_id, delta.delta_ns);
  }
  const auto has_nonzero = std::ranges::any_of(deltas, [](const auto& delta) -> bool { return delta.second != 0.0; });
  if (has_nonzero) {
    // Clock propagation has already validated a DAG with one active input per
    // clock buffer. Follow the real net/buffer edges, including generated roots.
    const auto insertions = std::move(deltas);
    deltas.clear();
    for (const auto& [start_node, amount] : insertions) {
      if (amount == 0.0) {
        continue;
      }
      std::vector<bool> seen(context.nodes.size(), false);
      std::vector<FastStaNodeId> pending{start_node};
      while (!pending.empty()) {
        const auto node_id = pending.back();
        pending.pop_back();
        if (seen.at(node_id)) {
          continue;
        }
        seen.at(node_id) = true;
        deltas[node_id] += amount;
        const auto& node = context.nodes.at(node_id);
        if (node.clock_inactive) {
          continue;
        }
        if (node.kind == FastStaNodeKind::kBufferInput) {
          const auto output = context.buffer_output_node_id_by_inst.find(node.inst_name);
          if (output != context.buffer_output_node_id_by_inst.end()) {
            pending.push_back(output->second);
          }
        }
        for (const auto net_id : node.output_net_ids) {
          for (const auto load_id : context.nets.at(net_id).load_node_ids) {
            if (context.nodes.at(load_id).domain == FastStaNodeDomain::kClock) {
              pending.push_back(load_id);
            }
          }
        }
      }
    }
  } else {
    deltas.clear();
  }
  std::vector<FastStaTimingRelationFact> separated_relations;
  if (deltas.empty()) {
    separated_relations = context.timing_relations;
  } else {
    auto analysis = AnalyzeLogic(context, deltas);
    if (!analysis.complete()) {
      result.diagnostic = analysis.diagnostic;
      return result;
    }
    separated_relations = std::move(analysis.relations);
  }
  result.evaluated_check_count = separated_relations.size();
  for (const auto& relation : separated_relations) {
    const auto floor = relation.check == FastStaTimingCheckKind::kSetup ? query.setup_floor_ns : query.hold_floor_ns;
    if (relation.slack_ns < floor) {
      result.violated.push_back(relation);
    } else if (relation.slack_ns - floor <= std::max(0.0, query.near_active_ns)) {
      result.near_active.push_back(relation);
    }
  }
  const auto order
      = [](const auto& lhs, const auto& rhs) -> bool { return std::pair(lhs.slack_ns, lhs.relation_id) < std::pair(rhs.slack_ns, rhs.relation_id); };
  std::ranges::sort(result.violated, order);
  std::ranges::sort(result.near_active, order);
  if (query.maximum_near_active > 0U && result.near_active.size() > query.maximum_near_active) {
    result.near_active.resize(query.maximum_near_active);
  }
  result.complete = true;
  result.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return result;
}

}  // namespace icts
