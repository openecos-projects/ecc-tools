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
 * @file FastSTAContextStore.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Capture and restore exact clock, logic and topology transaction state.
 */

#include "FastSTAContextStore.hh"

#include <cstddef>
#include <memory>
#include <ranges>
#include <unordered_set>
#include <utility>
#include <vector>

#include "clock_sizing/FastSTAIncremental.hh"
#include "power/FastSTAPower.hh"
#include "timing/FastSTATiming.hh"

namespace icts {

FastSTA::ContextStore::LogicTimingSnapshot::LogicTimingSnapshot(FastStaNodeId node_id, const FastStaNode& node)
    : id(node_id),
      scalar(node.timing),
      early(node.early_timing),
      late(node.late_timing),
      tagged_early(node.tagged_early_timing),
      tagged_late(node.tagged_late_timing)
{
}

void FastSTA::ContextStore::LogicTimingSnapshot::swap(FastStaContext& context)
{
  auto& node = context.nodes.at(id);
  std::swap(scalar, node.timing);
  std::swap(early, node.early_timing);
  std::swap(late, node.late_timing);
  std::swap(tagged_early, node.tagged_early_timing);
  std::swap(tagged_late, node.tagged_late_timing);
}

FastSTA::ContextStore::NodePreparationSnapshot::NodePreparationSnapshot(FastStaNodeId node_id, const FastStaNode& node)
    : id(node_id),
      kind(node.kind),
      domain(node.domain),
      clock_name(node.clock_name),
      clock_from_port(node.clock_from_port),
      clock_to_port(node.clock_to_port),
      case_value(node.case_value),
      caps(node.input_cap_pf_by_timing),
      input_cap(node.input_cap_pf),
      clock_inactive(node.clock_inactive),
      clock_edge_triggered(node.clock_edge_triggered),
      clock_trigger_transition(node.clock_trigger_transition),
      cap_available(node.input_cap_profile_available)
{
}

void FastSTA::ContextStore::NodePreparationSnapshot::swap(FastStaContext& context)
{
  auto& node = context.nodes.at(id);
  std::swap(kind, node.kind);
  std::swap(domain, node.domain);
  std::swap(clock_name, node.clock_name);
  std::swap(clock_from_port, node.clock_from_port);
  std::swap(clock_to_port, node.clock_to_port);
  std::swap(case_value, node.case_value);
  std::swap(caps, node.input_cap_pf_by_timing);
  std::swap(input_cap, node.input_cap_pf);
  std::swap(clock_inactive, node.clock_inactive);
  std::swap(clock_edge_triggered, node.clock_edge_triggered);
  std::swap(clock_trigger_transition, node.clock_trigger_transition);
  std::swap(cap_available, node.input_cap_profile_available);
}

FastSTA::ContextStore::ClockTopologySnapshot::ClockTopologySnapshot(const FastStaContext& context, const std::unordered_set<FastStaNodeId>& complete_nodes)
    : node_suffix(context.nodes.begin() + static_cast<std::ptrdiff_t>(context.input_node_count), context.nodes.end()),
      net_suffix(context.nets.begin() + static_cast<std::ptrdiff_t>(context.input_net_count), context.nets.end()),
      initial_kinds(context.initial_node_kinds),
      initial_node_domains(context.initial_node_domains),
      initial_net_domains(context.initial_net_domains),
      initial_inputs(context.initial_buffer_inputs),
      initial_outputs(context.initial_buffer_outputs),
      inputs(context.buffer_input_node_id_by_inst),
      outputs(context.buffer_output_node_id_by_inst),
      case_values(context.case_values),
      io_caps(context.unconstrained_io_caps),
      constraints(context.constraints),
      clock_sources(context.clock_source_node_ids),
      owned_nodes(context.owned_clock_node_ids),
      owned_sinks(context.owned_clock_sink_node_ids),
      overlay_nodes(context.clock_overlay_node_ids),
      overlay_nets(context.clock_overlay_net_ids),
      routes(context.clock_routes),
      logic_preparation(context.logic_preparation),
      source(context.source_node_id),
      clock_name(context.clock_name),
      period(context.clock_period_ns),
      owner_scope(context.owner_clock_scope_available)
{
  for (FastStaNodeId id = 0U; id < context.input_node_count; ++id) {
    if (!complete_nodes.contains(id)) {
      preparation.emplace_back(id, context.nodes.at(id));
    }
  }
}

void FastSTA::ContextStore::ClockTopologySnapshot::swap(FastStaContext& context)
{
  // Name and location indices for the stable prefix are never rebuilt.
  // Only the synthesized suffix leaves/enters the maintained maps.
  std::vector<FastStaNode> current_nodes;
  current_nodes.reserve(context.nodes.size() - context.input_node_count);
  for (FastStaNodeId id = context.input_node_count; id < context.nodes.size(); ++id) {
    auto& node = context.nodes.at(id);
    context.node_id_by_name.erase(node.name);
    const auto location = context.node_id_by_location.find({node.location.x_dbu, node.location.y_dbu});
    if (location != context.node_id_by_location.end() && location->second == id) {
      context.node_id_by_location.erase(location);
    }
    current_nodes.push_back(std::move(node));
  }
  context.nodes.resize(context.input_node_count);
  for (auto& node : node_suffix) {
    const auto id = context.nodes.size();
    context.node_id_by_name.emplace(node.name, id);
    context.node_id_by_location.emplace(std::pair{node.location.x_dbu, node.location.y_dbu}, id);
    context.nodes.push_back(std::move(node));
  }
  node_suffix = std::move(current_nodes);
  std::vector<FastStaNet> current_nets;
  current_nets.reserve(context.nets.size() - context.input_net_count);
  for (FastStaNetId id = context.input_net_count; id < context.nets.size(); ++id) {
    auto& net = context.nets.at(id);
    context.net_id_by_name.erase(net.name);
    current_nets.push_back(std::move(net));
  }
  context.nets.resize(context.input_net_count);
  for (auto& net : net_suffix) {
    context.net_id_by_name.emplace(net.name, context.nets.size());
    context.nets.push_back(std::move(net));
  }
  net_suffix = std::move(current_nets);
  for (auto& state : preparation) {
    state.swap(context);
  }
  std::swap(initial_kinds, context.initial_node_kinds);
  std::swap(initial_node_domains, context.initial_node_domains);
  std::swap(initial_net_domains, context.initial_net_domains);
  std::swap(initial_inputs, context.initial_buffer_inputs);
  std::swap(initial_outputs, context.initial_buffer_outputs);
  std::swap(inputs, context.buffer_input_node_id_by_inst);
  std::swap(outputs, context.buffer_output_node_id_by_inst);
  std::swap(case_values, context.case_values);
  std::swap(io_caps, context.unconstrained_io_caps);
  std::swap(constraints, context.constraints);
  std::swap(clock_sources, context.clock_source_node_ids);
  std::swap(owned_nodes, context.owned_clock_node_ids);
  std::swap(owned_sinks, context.owned_clock_sink_node_ids);
  std::swap(overlay_nodes, context.clock_overlay_node_ids);
  std::swap(overlay_nets, context.clock_overlay_net_ids);
  std::swap(routes, context.clock_routes);
  std::swap(logic_preparation, context.logic_preparation);
  std::swap(source, context.source_node_id);
  std::swap(clock_name, context.clock_name);
  std::swap(period, context.clock_period_ns);
  std::swap(owner_scope, context.owner_clock_scope_available);
}

FastSTA::ContextStore::ClockTrialNodeSnapshot::ClockTrialNodeSnapshot(FastStaNodeId node_id, const FastStaNode& node)
    : id(node_id),
      timing(node.timing),
      early(node.early_timing),
      late(node.late_timing),
      power{node.internal_power_w, node.leakage_power_w, node.area_um2},
      inactive(node.clock_inactive)
{
}

void FastSTA::ContextStore::ClockTrialNodeSnapshot::swap(FastStaContext& context)
{
  auto& node = context.nodes.at(id);
  std::swap(node.timing, timing);
  std::swap(node.early_timing, early);
  std::swap(node.late_timing, late);
  std::swap(node.internal_power_w, power.at(0U));
  std::swap(node.leakage_power_w, power.at(1U));
  std::swap(node.area_um2, power.at(2U));
  std::swap(node.clock_inactive, inactive);
}

FastSTA::ContextStore::ClockTrialNetSnapshot::ClockTrialNetSnapshot(FastStaNetId net_id, FastStaNet& net)
    : id(net_id), drivers(std::move(net.driver_timing_by_state)), switching_power_w(net.switching_power_w), max_cap_pf(net.max_cap_pf)
{
}

void FastSTA::ContextStore::ClockTrialNetSnapshot::swap(FastStaContext& context)
{
  auto& net = context.nets.at(id);
  std::swap(net.driver_timing_by_state, drivers);
  std::swap(net.switching_power_w, switching_power_w);
  std::swap(net.max_cap_pf, max_cap_pf);
}

FastSTA::ContextStore::TimingEditSnapshot::TimingEditSnapshot(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes,
                                                              const FastStaDirtyRegion* region, const std::vector<FastStaNodeId>* affected_logic)
    : skew(context.skew),
      summary(context.timing_summary),
      power(context.power),
      complete(affected_logic != nullptr),
      timing_valid(context.timing_valid),
      power_valid(context.power_valid),
      logic_tags_valid(context.logic_tags_valid),
      clock_timing_valid(context.clock_timing_valid),
      clock_trial_active(context.clock_trial_active)
{
  // Only explicit recovery from an invalid starting state needs a cold
  // snapshot. Normal complete updates keep unchanged logic topology/RC resident.
  if (region == nullptr) {
    cold_state = std::make_unique<FastStaContext>(context);
    return;
  }
  const bool prepared_clock_trial = affected_logic == nullptr && context.power_valid;
  std::unordered_set<FastStaNodeId> node_ids(region->node_ids.begin(), region->node_ids.end());
  std::unordered_set<FastStaNetId> net_ids(region->net_ids.begin(), region->net_ids.end());
  std::unordered_set<FastStaNodeId> changed_node_ids;
  std::unordered_set<FastStaNetId> additional_net_ids;
  for (const auto& change : changes) {
    const auto& changed_node = context.nodes.at(change.node_id);
    const auto input = context.buffer_input_node_id_by_inst.at(changed_node.inst_name);
    const auto output = context.buffer_output_node_id_by_inst.at(changed_node.inst_name);
    node_ids.insert(input);
    node_ids.insert(output);
    changed_node_ids.insert(input);
    changed_node_ids.insert(output);
    const auto incoming = context.nodes.at(input).incoming_net_id;
    if (incoming < context.nets.size() && net_ids.insert(incoming).second) {
      additional_net_ids.insert(incoming);
    }
    for (const auto net_id : context.nodes.at(output).output_net_ids) {
      if (net_ids.insert(net_id).second) {
        additional_net_ids.insert(net_id);
      }
    }
  }
  nodes.reserve(prepared_clock_trial ? changed_node_ids.size() : node_ids.size());
  if (prepared_clock_trial) {
    clock_nodes.reserve(node_ids.size() - changed_node_ids.size());
    clock_nets.reserve(net_ids.size());
  }
  for (const auto id : node_ids) {
    if (prepared_clock_trial && !changed_node_ids.contains(id)) {
      clock_nodes.emplace_back(id, context.nodes.at(id));
    } else {
      nodes.emplace_back(id, context.nodes.at(id));
    }
  }
  const auto& load_update_ids = region->load_update_net_ids.empty() ? region->net_ids : region->load_update_net_ids;
  const std::unordered_set<FastStaNetId> load_update_nets(load_update_ids.begin(), load_update_ids.end());
  for (const auto id : net_ids) {
    if (prepared_clock_trial && !load_update_nets.contains(id) && !additional_net_ids.contains(id)) {
      // Regional propagation resets these drivers before reading them.
      // Keep their unchanged topology/RC resident and move the old result
      // into the undo record. Load-changing nets retain the full RC state.
      clock_nets.emplace_back(id, context.nets.at(id));
    } else {
      nets.emplace_back(id, context.nets.at(id));
    }
  }
  if (!context.power_valid) {
    for (FastStaNodeId id = 0U; id < context.nodes.size(); ++id) {
      if (!node_ids.contains(id)) {
        const auto& node = context.nodes.at(id);
        additional_node_power.push_back({id, {node.internal_power_w, node.leakage_power_w, node.area_um2}});
      }
    }
    for (FastStaNetId id = 0U; id < context.nets.size(); ++id) {
      if (!net_ids.contains(id)) {
        additional_net_power.emplace_back(id, context.nets.at(id).switching_power_w);
      }
    }
  }
  for (std::size_t index = 0; index < context.timing_arcs.size(); ++index) {
    const auto& arc = context.timing_arcs.at(index);
    const auto& arc_nodes = prepared_clock_trial ? changed_node_ids : node_ids;
    if (arc_nodes.contains(arc.from_node_id) || arc_nodes.contains(arc.to_node_id)) {
      arcs.emplace_back(index, arc);
    }
  }
  if (affected_logic != nullptr) {
    for (const auto id : *affected_logic) {
      if (!node_ids.contains(id)) {
        logic.emplace_back(id, context.nodes.at(id));
      }
    }
    relations = context.timing_relations;
    seeds = context.timing_relation_seeds;
  }
}

void FastSTA::ContextStore::TimingEditSnapshot::swap(FastStaContext& context)
{
  if (cold_state != nullptr) {
    std::swap(context, *cold_state);
    return;
  }
  if (topology != nullptr) {
    topology->swap(context);
  }
  for (auto& [id, node] : nodes) {
    std::swap(context.nodes.at(id), node);
  }
  for (auto& [id, net] : nets) {
    std::swap(context.nets.at(id), net);
  }
  for (auto& state : clock_nodes) {
    state.swap(context);
  }
  for (auto& state : clock_nets) {
    state.swap(context);
  }
  for (auto& [id, arc] : arcs) {
    std::swap(context.timing_arcs.at(id), arc);
  }
  for (auto& state : logic) {
    state.swap(context);
  }
  for (auto& [id, values] : additional_node_power) {
    auto& node = context.nodes.at(id);
    std::swap(node.internal_power_w, values.at(0U));
    std::swap(node.leakage_power_w, values.at(1U));
    std::swap(node.area_um2, values.at(2U));
  }
  for (auto& [id, value] : additional_net_power) {
    std::swap(context.nets.at(id).switching_power_w, value);
  }
  if (complete) {
    std::swap(context.timing_relations, relations);
    std::swap(context.timing_relation_seeds, seeds);
  }
  std::swap(context.skew, skew);
  std::swap(context.timing_summary, summary);
  std::swap(context.power, power);
  std::swap(context.timing_valid, timing_valid);
  std::swap(context.clock_timing_valid, clock_timing_valid);
  std::swap(context.power_valid, power_valid);
  std::swap(context.logic_tags_valid, logic_tags_valid);
  std::swap(context.clock_trial_active, clock_trial_active);
}

void FastSTA::ContextStore::ContextTransaction::activate(FastStaContext& context, bool pending)
{
  if (pending == pending_active) {
    return;
  }
  if (pending) {
    for (auto& edit : edits) {
      edit.swap(context);
    }
  } else {
    for (auto& edit : std::views::reverse(edits)) {
      edit.swap(context);
    }
  }
  pending_active = pending;
}

auto FastSTA::ContextStore::applyBufferMasters(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes, bool update_power,
                                               ContextTransaction* transaction) -> bool
{
  if (!FastStaIncremental::validateBufferMasterChanges(context, changes)) {
    return false;
  }
  if (!context.timing_valid || ((!context.constraints.clocks.empty() || !context.constraints.path_exceptions.empty()) && !context.logic_tags_valid)) {
    if (transaction != nullptr) {
      return false;
    }
    TimingEditSnapshot original(context, changes);
    if (!FastStaIncremental::changeBufferMasters(context, changes) || !FastStaTiming::update(context) || (update_power && !FastStaPower::update(context))) {
      original.swap(context);
      return false;
    }
    context.timing_summary.used_full_rebuild = true;
    context.timing_summary.fallback_reason = "master_edit_fallback:prior_timing_state_unavailable";
    return true;
  }
  const auto region = FastStaIncremental::describeBufferMasterRegion(context, changes);
  if (!region.has_value()) {
    return false;
  }
  const auto affected_logic = FastStaTiming::collectAffectedLogicNodes(context, *region);
  if (!affected_logic.has_value()) {
    return false;
  }
  TimingEditSnapshot original(context, changes, &*region, &*affected_logic);
  const auto changed = FastStaIncremental::changeBufferMastersIncremental(context, changes);
  if (!changed.has_value() || !FastStaTiming::updateRegion(context, *changed)
      || (update_power && !(original.power_valid ? FastStaPower::updatePreparedRegion(context, *changed) : FastStaPower::update(context)))) {
    original.swap(context);
    return false;
  }
  if (!update_power) {
    context.power_valid = false;
  }
  if (transaction != nullptr) {
    transaction->edits.push_back(std::move(original));
  }
  return true;
}

}  // namespace icts
