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
 * @file FastSTAContextQueries.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Export validated context timing, topology and electrical facts.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "branch_timing/FastSTABranch.hh"
#include "clock_state/FastSTAClockState.hh"
#include "timing/FastSTAConstraints.hh"
#include "timing/FastSTATiming.hh"

namespace icts {
namespace {

auto clockOwnerMask(const FastStaContext& context, bool sinks = false) -> std::vector<bool>
{
  std::vector<bool> mask(context.nodes.size(), !context.owner_clock_scope_available);
  if (context.owner_clock_scope_available) {
    for (const auto id : sinks ? context.owned_clock_sink_node_ids : context.owned_clock_node_ids) {
      if (id < mask.size()) {
        mask.at(id) = true;
      }
    }
  }
  return mask;
}

auto toSlewRole(FastStaNodeKind kind) -> FastStaSlewRole
{
  switch (kind) {
    case FastStaNodeKind::kBufferInput:
      return FastStaSlewRole::kBufferInput;
    case FastStaNodeKind::kSink:
      return FastStaSlewRole::kSink;
    case FastStaNodeKind::kSource:
    case FastStaNodeKind::kBufferOutput:
      return FastStaSlewRole::kUnknown;
  }
  return FastStaSlewRole::kUnknown;
}

auto toClockPointKind(FastStaNodeKind kind) -> FastStaClockPointKind
{
  switch (kind) {
    case FastStaNodeKind::kSource:
      return FastStaClockPointKind::kSource;
    case FastStaNodeKind::kBufferInput:
      return FastStaClockPointKind::kBufferInput;
    case FastStaNodeKind::kBufferOutput:
      return FastStaClockPointKind::kBufferOutput;
    case FastStaNodeKind::kSink:
      return FastStaClockPointKind::kSink;
  }
  return FastStaClockPointKind::kSink;
}

auto makeGraphProfile(const FastStaContext& context) -> FastStaGraphProfile
{
  FastStaGraphProfile profile;
  profile.node_count = context.nodes.size();
  profile.net_count = context.nets.size();
  const auto owner = clockOwnerMask(context);
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.domain == FastStaNodeDomain::kLogic) {
      ++profile.logic_node_count;
      continue;
    }
    profile.owned_clock_node_count += owner.at(node_id) ? 1U : 0U;
    switch (node.kind) {
      case FastStaNodeKind::kSink:
        ++profile.sink_count;
        break;
      case FastStaNodeKind::kBufferInput:
        ++profile.buffer_input_count;
        break;
      case FastStaNodeKind::kBufferOutput:
        ++profile.buffer_output_count;
        break;
      case FastStaNodeKind::kSource:
        break;
    }
  }
  for (const auto& net : context.nets) {
    profile.logic_net_count += net.domain == FastStaNetDomain::kLogic ? 1U : 0U;
  }
  profile.timing_arc_count = context.timing_arcs.size();
  profile.timing_check_count = context.timing_checks.size();
  return profile;
}

auto hasCompleteBufferPairIndexes(const FastStaContext& context) -> bool
{
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kBufferInput && node.kind != FastStaNodeKind::kBufferOutput) {
      continue;
    }
    const auto& index = node.kind == FastStaNodeKind::kBufferInput ? context.buffer_input_node_id_by_inst : context.buffer_output_node_id_by_inst;
    const auto indexed = index.find(node.inst_name);
    if (node.inst_name.empty() || indexed == index.end() || indexed->second != node_id) {
      return false;
    }
    const auto& peer_index = node.kind == FastStaNodeKind::kBufferInput ? context.buffer_output_node_id_by_inst : context.buffer_input_node_id_by_inst;
    const auto peer = peer_index.find(node.inst_name);
    if (peer == peer_index.end() || peer->second >= context.nodes.size()) {
      return false;
    }
    const auto& peer_node = context.nodes.at(peer->second);
    const auto expected_peer_kind = node.kind == FastStaNodeKind::kBufferInput ? FastStaNodeKind::kBufferOutput : FastStaNodeKind::kBufferInput;
    if (peer_node.kind != expected_peer_kind || peer_node.inst_name != node.inst_name) {
      return false;
    }
  }
  return true;
}

auto makeClockTreeTopology(const FastStaContext& context) -> std::optional<FastStaClockTreeTopology>
{
  if (!hasCompleteBufferPairIndexes(context)) {
    return std::nullopt;
  }
  FastStaClockTreeTopology topology;
  topology.source_node_id = context.source_node_id;
  topology.parent_by_node.assign(context.nodes.size(), kInvalidFastStaNodeId);
  const auto owner = clockOwnerMask(context);

  const auto find_buffer_input = [&](const std::string& inst_name) -> FastStaNodeId {
    if (const auto indexed = context.buffer_input_node_id_by_inst.find(inst_name); indexed != context.buffer_input_node_id_by_inst.end()) {
      if (indexed->second < context.nodes.size()) {
        const auto& node = context.nodes.at(indexed->second);
        if (node.kind == FastStaNodeKind::kBufferInput && node.inst_name == inst_name) {
          return indexed->second;
        }
      }
    }
    return kInvalidFastStaNodeId;
  };

  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (!owner.at(node_id) || node.domain != FastStaNodeDomain::kClock) {
      continue;
    }
    if (node.kind == FastStaNodeKind::kBufferOutput) {
      const auto input_node_id = find_buffer_input(node.inst_name);
      if (input_node_id != kInvalidFastStaNodeId && owner.at(input_node_id)) {
        topology.parent_by_node.at(node_id) = input_node_id;
      }
      continue;
    }
    if (node.incoming_net_id < context.nets.size() && owner.at(context.nets.at(node.incoming_net_id).driver_node_id)) {
      topology.parent_by_node.at(node_id) = context.nets.at(node.incoming_net_id).driver_node_id;
    }
  }

  topology.children_by_node.assign(context.nodes.size(), {});
  for (FastStaNodeId node_id = 0U; node_id < topology.parent_by_node.size(); ++node_id) {
    const auto parent_id = topology.parent_by_node.at(node_id);
    if (parent_id < topology.children_by_node.size()) {
      topology.children_by_node.at(parent_id).push_back(node_id);
    }
  }
  return topology;
}

}  // namespace

auto FastSTA::queryGraphProfile(FastStaContextId context_id) const -> std::optional<FastStaGraphProfile>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  return makeGraphProfile(*context);
}

auto FastSTA::queryAnalysisStatus(FastStaContextId context_id) const -> std::optional<FastStaAnalysisStatus>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  // A clock trial owns provisional clock timing and refreshes its actual
  // power. Full timing remains unpublished until the accepted update.
  return FastStaAnalysisStatus{.timing_valid = context->timing_valid && !context->clock_trial_active,
                               .power_valid = context->power_valid,
                               .clock_timing_valid = context->clock_timing_valid};
}

auto FastSTA::queryClockElectricalScope(FastStaContextId context_id) const -> std::optional<FastStaClockElectricalScope>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  FastStaClockElectricalScope scope;
  const auto owner = clockOwnerMask(*context);
  auto included = owner;
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    included.at(node_id) = included.at(node_id) && context->nodes.at(node_id).domain == FastStaNodeDomain::kClock;
  }
  for (FastStaNetId net_id = 0U; net_id < context->nets.size(); ++net_id) {
    const auto& net = context->nets.at(net_id);
    if (net.domain != FastStaNetDomain::kClock || net.driver_node_id >= owner.size() || !owner.at(net.driver_node_id)) {
      continue;
    }
    scope.net_ids.push_back(net_id);
    // Electrical loading belongs to the selected net even when a preserved
    // boundary pin is not itself an optimizable clock cell.
    for (const auto load_id : net.load_node_ids) {
      if (load_id >= included.size()) {
        return std::nullopt;
      }
      included.at(load_id) = true;
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < included.size(); ++node_id) {
    if (included.at(node_id)) {
      scope.node_ids.push_back(node_id);
    }
  }
  return scope;
}

auto FastSTA::queryTimingSummary(FastStaContextId context_id) const -> std::optional<FastStaTimingSummary>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr || context->clock_trial_active) {
    return std::nullopt;
  }
  if (!context->timing_valid && context->timing_summary.status == FastStaTimingStatus::kComplete) {
    return FastStaTimingSummary{.status = FastStaTimingStatus::kNotRun, .fallback_reason = "timing_state_invalidated"};
  }
  return context->timing_summary;
}

auto FastSTA::queryClockTreeTopology(FastStaContextId context_id) const -> std::optional<FastStaClockTreeTopology>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  return makeClockTreeTopology(*context);
}

auto FastSTA::collectClockSizingBuffers(FastStaContextId context_id) const -> std::vector<FastStaClockSizingBuffer>
{
  std::vector<FastStaClockSizingBuffer> buffers;
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return buffers;
  }
  if (!hasCompleteBufferPairIndexes(*context)) {
    return buffers;
  }
  buffers.reserve(context->nodes.size());
  const auto owner = clockOwnerMask(*context);
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (!owner.at(node_id) || node.domain != FastStaNodeDomain::kClock || node.kind != FastStaNodeKind::kBufferOutput || node.inst_name.empty()
        || node.cell_master.empty()) {
      continue;
    }
    const auto model = context->liberty_cell_by_master.find(node.cell_master);
    if (node.clock_edge_triggered || (model != context->liberty_cell_by_master.end() && model->second.clock_gate.has_value())) {
      continue;
    }
    buffers.push_back(FastStaClockSizingBuffer{.node_id = node_id, .inst_name = node.inst_name, .cell_master = node.cell_master});
  }
  return buffers;
}

auto FastSTA::collectClockSinkArrivals(FastStaContextId context_id) const -> std::vector<FastStaClockSinkArrival>
{
  std::vector<FastStaClockSinkArrival> sinks;
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return sinks;
  }
  sinks.reserve(context->nodes.size());
  const auto owner = clockOwnerMask(*context, true);
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (!owner.at(node_id) || node.domain != FastStaNodeDomain::kClock || (!context->owner_clock_scope_available && node.kind != FastStaNodeKind::kSink)
        || !context->clock_timing_valid || !node.timing.valid) {
      continue;
    }
    sinks.push_back(FastStaClockSinkArrival{.node_id = node_id, .sink_name = node.name, .arrival_ns = node.timing.arrival_ns, .slew_ns = node.timing.slew_ns});
  }
  return sinks;
}

auto FastSTA::collectClockPointFacts(FastStaContextId context_id) const -> std::vector<FastStaClockPointFact>
{
  std::vector<FastStaClockPointFact> facts;
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->clock_timing_valid) {
    return facts;
  }
  facts.reserve(context->nodes.size());
  for (const auto& node : context->nodes) {
    if (node.domain == FastStaNodeDomain::kClock && node.timing.valid) {
      facts.push_back(FastStaClockPointFact{
          .pin_name = node.name,
          .kind = toClockPointKind(node.kind),
          .arrival_ns = node.timing.arrival_ns,
          .slew_ns = node.timing.slew_ns,
      });
    }
  }
  return facts;
}

auto FastSTA::queryClockNodeArrival(FastStaContextId context_id, FastStaNodeId node_id) const -> std::optional<double>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr || !(context->timing_valid || context->clock_timing_valid) || node_id >= context->nodes.size()
      || !context->nodes.at(node_id).timing.valid) {
    return std::nullopt;
  }
  if (context->clock_trial_active && context->nodes.at(node_id).domain != FastStaNodeDomain::kClock) {
    return std::nullopt;
  }
  return context->nodes.at(node_id).timing.arrival_ns;
}

auto FastSTA::querySkew(FastStaContextId context_id) const -> FastStaSkewSummary
{
  const auto* context = queryContext(context_id);
  return context == nullptr || !context->clock_timing_valid ? FastStaSkewSummary{} : context->skew;
}

auto FastSTA::queryCapStatus(FastStaContextId context_id, FastStaNetId net_id) const -> std::optional<FastStaCapStatus>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->clock_timing_valid || net_id >= context->nets.size() || !context->nets.at(net_id).parasitic.valid) {
    return std::nullopt;
  }
  const auto& net = context->nets.at(net_id);
  double load_cap = 0.0;
  for (const auto& analysis : net.parasitic.driver_pi_by_timing) {
    for (const auto& pi : analysis) {
      load_cap = std::max(load_cap, pi.near_cap_pf + pi.far_cap_pf);
    }
  }
  return FastStaCapStatus{.net_id = net_id,
                          .net_name = net.name,
                          .load_cap_pf = load_cap,
                          .max_cap_pf = net.max_cap_pf,
                          .violated = net.max_cap_pf > 0.0 && load_cap > net.max_cap_pf,
                          .constraint_available = net.max_cap_pf > 0.0};
}

auto FastSTA::querySlewStatus(FastStaContextId context_id, FastStaNodeId node_id) const -> std::optional<FastStaSlewStatus>
{
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->clock_timing_valid || node_id >= context->nodes.size()) {
    return std::nullopt;
  }
  const auto& node = context->nodes.at(node_id);
  if (context->clock_trial_active && node.domain != FastStaNodeDomain::kClock) {
    return std::nullopt;
  }
  std::optional<double> slew;
  for (const auto* states : {&node.early_timing, &node.late_timing}) {
    for (const auto& point : *states) {
      if (point.valid) {
        slew = std::max(slew.value_or(0.0), point.slew_ns);
      }
    }
  }
  if (!slew.has_value()) {
    return std::nullopt;
  }
  return FastStaSlewStatus{.node_id = node_id,
                           .node_name = node.name,
                           .role = toSlewRole(node.kind),
                           .slew_ns = *slew,
                           .max_slew_ns = node.max_slew_ns,
                           .violated = node.max_slew_ns > 0.0 && *slew > node.max_slew_ns,
                           .constraint_available = node.max_slew_ns > 0.0};
}

auto FastSTA::queryPower(FastStaContextId context_id) const -> std::optional<FastStaPowerSummary>
{
  const auto* context = queryContext(context_id);
  return context == nullptr || !context->power_valid ? std::nullopt : std::optional<FastStaPowerSummary>{context->power};
}

auto FastSTA::collectTimingRelations(FastStaContextId context_id) const -> std::vector<FastStaTimingRelationFact>
{
  const auto* context = queryContext(context_id);
  return context == nullptr || !context->timing_valid || context->clock_trial_active ? std::vector<FastStaTimingRelationFact>{} : context->timing_relations;
}

auto FastSTA::collectTimingPointFacts(FastStaContextId context_id) const -> std::vector<FastStaTimingPointFact>
{
  std::vector<FastStaTimingPointFact> facts;
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->timing_valid || context->clock_trial_active) {
    return facts;
  }
  facts.reserve(context->nodes.size() * 4U);
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      const auto transition_index = transition == FastStaTransition::kRise ? 0U : 1U;
      for (const auto analysis : {FastStaAnalysisKind::kEarly, FastStaAnalysisKind::kLate}) {
        const auto& timing = analysis == FastStaAnalysisKind::kEarly ? node.early_timing.at(transition_index) : node.late_timing.at(transition_index);
        if (!timing.valid || timing.launch_node_id >= context->nodes.size()) {
          continue;
        }
        facts.push_back(FastStaTimingPointFact{
            .pin_name = node.name,
            .launch_pin_name = context->nodes.at(timing.launch_node_id).name,
            .transition = transition,
            .analysis = analysis,
            .arrival_ns
            = timing.arrival_ns
              + (node.domain == FastStaNodeDomain::kClock ? FastStaConstraints::phase(*context, timing.clock_name, timing.launch_clock_transition) : 0.0),
            .slew_ns = timing.slew_ns});
      }
    }
  }
  return facts;
}

auto FastSTA::collectTimingStageFacts(FastStaContextId context_id, std::optional<FastStaNodeId> node_filter) const -> std::vector<FastStaTimingStageFact>
{
  std::vector<FastStaTimingStageFact> facts;
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->timing_valid || context->clock_trial_active) {
    return facts;
  }
  if (node_filter.has_value() && (*node_filter >= context->nodes.size() || context->nodes.at(*node_filter).domain != FastStaNodeDomain::kLogic)) {
    return facts;
  }
  std::unordered_set<FastStaNodeId> launch_nodes;
  std::unordered_set<FastStaNodeId> cell_output_nodes;
  if (!node_filter.has_value()) {
    launch_nodes.reserve(context->timing_launches.size());
    for (const auto& launch : context->timing_launches) {
      launch_nodes.insert(launch.output_node_id);
    }
    cell_output_nodes.reserve(context->timing_arcs.size());
    for (const auto& arc : context->timing_arcs) {
      cell_output_nodes.insert(arc.to_node_id);
    }
  }
  facts.reserve(node_filter.has_value() ? 4U : context->nodes.size() * 2U);
  const auto end_node_id = node_filter.has_value() ? *node_filter + 1U : context->nodes.size();
  for (FastStaNodeId node_id = node_filter.value_or(0U); node_id < end_node_id; ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.domain != FastStaNodeDomain::kLogic) {
      continue;
    }
    auto stage = FastStaTimingStage::kNetLoad;
    if (node_filter.has_value()
            ? std::ranges::any_of(context->timing_launches, [node_id](const auto& launch) -> bool { return launch.output_node_id == node_id; })
            : launch_nodes.contains(node_id)) {
      stage = FastStaTimingStage::kLaunch;
    } else if (node_filter.has_value() ? std::ranges::any_of(context->timing_arcs, [node_id](const auto& arc) -> bool { return arc.to_node_id == node_id; })
                                       : cell_output_nodes.contains(node_id)) {
      stage = FastStaTimingStage::kCellOutput;
    }
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      const auto transition_index = transition == FastStaTransition::kRise ? 0U : 1U;
      for (const auto analysis : {FastStaAnalysisKind::kEarly, FastStaAnalysisKind::kLate}) {
        const auto& timing = analysis == FastStaAnalysisKind::kEarly ? node.early_timing.at(transition_index) : node.late_timing.at(transition_index);
        if (!timing.valid || timing.launch_node_id >= context->nodes.size()) {
          continue;
        }
        FastStaTimingStageFact fact{
            .pin_name = node.name,
            .input_pin_name = timing.stage_input_node_id < context->nodes.size() ? context->nodes.at(timing.stage_input_node_id).name : std::string{},
            .launch_pin_name = context->nodes.at(timing.launch_node_id).name,
            .cell_master = {},
            .from_port = {},
            .to_port = {},
            .net_name = {},
            .driver_library_name = {},
            .load_library_name = {},
            .transition = transition,
            .input_transition = timing.stage_input_transition,
            .analysis = analysis,
            .stage = stage,
            .selected_variant_index = timing.driver_arc_variant_index,
            .input_arrival_ns = timing.stage_input_arrival_ns,
            .input_slew_ns = timing.stage_input_slew_ns,
            .arrival_ns = timing.arrival_ns,
            .slew_ns = timing.slew_ns,
            .stage_delay_ns = timing.stage_delay_ns};
        if (timing.driver_is_launch && timing.driver_model_index < context->timing_launches.size()) {
          const auto& launch = context->timing_launches.at(timing.driver_model_index);
          const auto cell_iter = context->liberty_cell_by_master.find(launch.cell_master);
          if (cell_iter != context->liberty_cell_by_master.end() && timing.driver_arc_variant_index < cell_iter->second.timing_arcs.size()) {
            const auto& arc = cell_iter->second.timing_arcs.at(timing.driver_arc_variant_index);
            fact.driver_library_name = arc.library_name.empty() ? cell_iter->second.library_name : arc.library_name;
          }
        } else if (timing.driver_model_index < context->timing_arcs.size()) {
          const auto& timing_arc = context->timing_arcs.at(timing.driver_model_index);
          const auto cell_iter = context->liberty_cell_by_master.find(timing_arc.cell_master);
          if (cell_iter != context->liberty_cell_by_master.end() && timing.driver_arc_variant_index < cell_iter->second.timing_arcs.size()) {
            const auto& arc = cell_iter->second.timing_arcs.at(timing.driver_arc_variant_index);
            fact.driver_library_name = arc.library_name.empty() ? cell_iter->second.library_name : arc.library_name;
          }
        }
        if (const auto load_cell_iter = context->liberty_cell_by_master.find(node.cell_master); load_cell_iter != context->liberty_cell_by_master.end()) {
          fact.load_library_name = load_cell_iter->second.library_name;
        }
        if (stage == FastStaTimingStage::kLaunch && timing.driver_model_index < context->timing_launches.size()) {
          const auto& launch = context->timing_launches.at(timing.driver_model_index);
          fact.cell_master = launch.cell_master;
          fact.from_port = launch.clock_port;
          fact.to_port = launch.output_port;
        } else if (stage == FastStaTimingStage::kCellOutput && timing.driver_model_index < context->timing_arcs.size()) {
          const auto& arc = context->timing_arcs.at(timing.driver_model_index);
          fact.cell_master = arc.cell_master;
          fact.from_port = arc.from_port;
          fact.to_port = arc.to_port;
        } else if (stage == FastStaTimingStage::kNetLoad && node.incoming_net_id < context->nets.size()) {
          fact.net_name = context->nets.at(node.incoming_net_id).name;
        }
        facts.push_back(std::move(fact));
      }
    }
  }
  return facts;
}

auto FastSTA::collectParasiticNetFacts(FastStaContextId context_id) const -> std::vector<FastStaParasiticNetFact>
{
  std::vector<FastStaParasiticNetFact> facts;
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return facts;
  }
  facts.reserve(context->nets.size());
  for (const auto& net : context->nets) {
    if (!net.parasitic.valid || net.driver_node_id >= context->nodes.size()) {
      continue;
    }
    FastStaParasiticNetFact fact;
    fact.net_name = net.name;
    fact.driver_pin_name = context->nodes.at(net.driver_node_id).name;
    fact.load_pin_names.reserve(net.load_node_ids.size());
    for (const auto load_node_id : net.load_node_ids) {
      if (load_node_id < context->nodes.size()) {
        fact.load_pin_names.push_back(context->nodes.at(load_node_id).name);
      }
    }
    fact.nodes.reserve(net.parasitic.rc_nodes.size());
    for (const auto& rc_node : net.parasitic.rc_nodes) {
      FastStaParasiticNodeFact node_fact;
      node_fact.node_name = rc_node.name;
      node_fact.capacitance_pf = rc_node.cap_pf;
      node_fact.wire_capacitance_pf = rc_node.wire_cap_pf;
      node_fact.pin_capacitance_pf = rc_node.pin_cap_pf;
      auto terminal_ids = rc_node.terminal_node_ids;
      if (rc_node.terminal_node_id < context->nodes.size()) {
        terminal_ids.push_back(rc_node.terminal_node_id);
      }
      std::ranges::sort(terminal_ids);
      terminal_ids.erase(std::ranges::unique(terminal_ids).begin(), terminal_ids.end());
      for (const auto terminal_id : terminal_ids) {
        if (terminal_id < context->nodes.size()) {
          node_fact.terminal_pin_names.push_back(context->nodes.at(terminal_id).name);
        }
      }
      fact.nodes.push_back(std::move(node_fact));
    }
    fact.edges.reserve(net.parasitic.rc_edges.size());
    for (const auto& edge : net.parasitic.rc_edges) {
      if (edge.from < fact.nodes.size() && edge.to < fact.nodes.size()) {
        fact.edges.push_back(FastStaParasiticEdgeFact{.source_node_index = edge.from, .target_node_index = edge.to, .resistance_ohm = edge.resistance_ohm});
      }
    }
    facts.push_back(std::move(fact));
  }
  std::ranges::sort(facts, {}, &FastStaParasiticNetFact::net_name);
  return facts;
}

auto FastSTA::collectTimingRelationSeeds(FastStaContextId context_id) const -> FastStaTimingSeedSet
{
  const auto* context = queryContext(context_id);
  return context == nullptr || !context->timing_valid || context->clock_trial_active ? FastStaTimingSeedSet{} : context->timing_relation_seeds;
}

auto FastSTA::separateTimingRelations(FastStaContextId context_id, const FastStaSeparationQuery& query) const -> FastStaSeparationResult
{
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->timing_valid) {
    FastStaSeparationResult result;
    result.diagnostic = "invalid_fast_sta_context";
    return result;
  }
  if (context->clock_trial_active) {
    FastStaSeparationResult result;
    result.diagnostic = "clock_trial_active";
    return result;
  }
  return FastStaTiming::separate(*context, query);
}

auto FastSTA::evaluateBranch(FastStaContextId context_id, const FastStaBranchRequest& request) const -> FastStaBranchResult
{
  const auto* context = queryContext(context_id);
  return context == nullptr || !context->timing_valid || context->clock_trial_active
             ? FastStaBranchResult{.diagnostic = "branch_context_unavailable", .loads = {}}
             : FastStaBranch::evaluate(*context, request);
}

auto FastSTA::collectPinCapacitanceFacts(FastStaContextId context_id) const -> std::vector<FastStaPinCapacitanceFact>
{
  std::vector<FastStaPinCapacitanceFact> facts;
  if (const auto* context = queryContext(context_id); context != nullptr) {
    facts.reserve(context->nodes.size());
    for (const auto& node : context->nodes) {
      facts.push_back({.pin_name = node.name, .capacitance_pf = node.input_cap_pf_by_timing});
    }
  }
  return facts;
}

auto FastSTA::collectPiElmoreFacts(FastStaContextId context_id) const -> std::vector<FastStaPiElmoreFact>
{
  std::vector<FastStaPiElmoreFact> facts;
  const auto* context = queryContext(context_id);
  if (context == nullptr) {
    return facts;
  }
  for (const auto& net : context->nets) {
    if (!net.parasitic.valid || net.driver_node_id >= context->nodes.size()) {
      continue;
    }
    for (std::size_t load = 0U; load < net.load_node_ids.size(); ++load) {
      if (load >= net.load_rc_node_ids.size() || net.load_rc_node_ids.at(load) >= net.parasitic.rc_nodes.size()) {
        continue;
      }
      for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
        for (std::size_t transition = 0U; transition < 2U; ++transition) {
          const auto& pi = net.parasitic.driver_pi_by_timing.at(analysis).at(transition);
          const auto& terminal = net.parasitic.rc_nodes.at(net.load_rc_node_ids.at(load));
          facts.push_back({.net_name = net.name,
                           .driver_pin_name = context->nodes.at(net.driver_node_id).name,
                           .load_pin_name = context->nodes.at(net.load_node_ids.at(load)).name,
                           .analysis = analysis == 0U ? FastStaAnalysisKind::kEarly : FastStaAnalysisKind::kLate,
                           .transition = transition == 0U ? FastStaTransition::kRise : FastStaTransition::kFall,
                           .near_cap_pf = pi.near_cap_pf,
                           .far_cap_pf = pi.far_cap_pf,
                           .resistance_ohm = pi.resistance_ohm,
                           .elmore_ns = terminal.elmore_delay_ns_by_timing.at(analysis).at(transition)});
        }
      }
    }
  }
  return facts;
}

}  // namespace icts
