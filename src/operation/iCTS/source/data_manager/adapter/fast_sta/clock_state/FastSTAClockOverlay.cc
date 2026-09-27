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
 * @file FastSTAClockOverlay.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Validate, splice and extract dependency-matched clock topology.
 */

#include "FastSTAClockOverlay.hh"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTABuilder.hh"
#include "FastSTAClockState.hh"
#include "FastSTAClockTree.hh"
#include "FastSTAGraphImport.hh"
#include "FastSTALibertyModel.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Net.hh"

namespace icts {
namespace fast_sta {

auto RequiresPropagationBufferModel(const FastStaNode& node) -> bool
{
  return node.kind == FastStaNodeKind::kBufferInput || node.kind == FastStaNodeKind::kBufferOutput;
}

auto AppendClock(const Clock& clock, bool owner, FastStaContext& context, std::string& failure_reason) -> bool
{
  auto clock_graph = FastStaClockTree::buildFromClock(clock);
  const bool first_clock = context.nodes.empty() && context.nets.empty() && context.node_id_by_name.empty();
  context.nodes.reserve(context.nodes.size() + clock_graph.nodes.size());
  context.nets.reserve(context.nets.size() + clock_graph.nets.size());
  if (first_clock) {
    // The first clock retains the graph's node IDs. Transfer its existing
    // indexes instead of allocating and hashing every pin a second time.
    context.node_id_by_name = std::move(clock_graph.node_id_by_name);
    context.node_id_by_location = std::move(clock_graph.node_id_by_location);
    context.buffer_input_node_id_by_inst = std::move(clock_graph.buffer_input_node_id_by_inst);
    context.buffer_output_node_id_by_inst = std::move(clock_graph.buffer_output_node_id_by_inst);
  } else {
    context.node_id_by_name.reserve(context.node_id_by_name.size() + clock_graph.nodes.size());
    context.net_id_by_name.reserve(context.net_id_by_name.size() + clock_graph.nets.size());
  }
  std::vector<FastStaNodeId> node_ids;
  node_ids.reserve(clock_graph.nodes.size());
  for (auto& source : clock_graph.nodes) {
    auto id = context.nodes.size();
    bool inserted = true;
    if (!first_clock) {
      const auto entry = context.node_id_by_name.try_emplace(source.name, id);
      id = entry.first->second;
      inserted = entry.second;
    }
    node_ids.push_back(id);
    if (inserted) {
      source.incoming_net_id = kInvalidFastStaNetId;
      source.output_net_ids.clear();
      if (!first_clock) {
        context.node_id_by_location.emplace(std::pair{source.location.x_dbu, source.location.y_dbu}, id);
      }
      context.nodes.push_back(std::move(source));
    } else {
      auto& current = context.nodes.at(id);
      if (current.inst_name != source.inst_name || current.cell_master != source.cell_master || current.location.x_dbu != source.location.x_dbu
          || current.location.y_dbu != source.location.y_dbu) {
        failure_reason = "clock_overlay_pin_identity_conflict:" + source.name;
        return false;
      }
      // Shared/generated boundaries keep their physical propagation role. The SDC
      // resolver subsequently assigns clock-source semantics independently.
      if (RequiresPropagationBufferModel(source) || current.kind == FastStaNodeKind::kSink) {
        current.kind = source.kind;
      }
    }
    const auto& current = context.nodes.at(id);
    if (!first_clock && current.kind == FastStaNodeKind::kBufferInput) {
      context.buffer_input_node_id_by_inst[current.inst_name] = id;
    } else if (!first_clock && current.kind == FastStaNodeKind::kBufferOutput) {
      context.buffer_output_node_id_by_inst[current.inst_name] = id;
    }
  }
  if (owner) {
    context.owner_clock_scope_available = true;
    context.owned_clock_node_ids = node_ids;
    context.owned_clock_sink_node_ids.reserve(context.owned_clock_sink_node_ids.size() + clock.get_loads().size());
    for (const auto* load : clock.get_loads()) {
      const auto node = context.node_id_by_name.find(Design::getPinFullName(load));
      if (node != context.node_id_by_name.end()) {
        context.owned_clock_sink_node_ids.push_back(node->second);
      }
    }
  }
  for (auto& net : clock_graph.nets) {
    if (net.driver_node_id >= node_ids.size()) {
      failure_reason = "clock_overlay_driver_unavailable:" + net.name;
      return false;
    }
    net.driver_node_id = node_ids.at(net.driver_node_id);
    std::unordered_set<FastStaNodeId> loads;
    loads.reserve(net.load_node_ids.size());
    for (auto& load : net.load_node_ids) {
      if (load >= node_ids.size()) {
        failure_reason = "clock_overlay_load_unavailable:" + net.name;
        return false;
      }
      load = node_ids.at(load);
      if (load == net.driver_node_id || !loads.insert(load).second) {
        failure_reason = "clock_overlay_load_duplicate:" + net.name;
        return false;
      }
    }
    if (const auto existing = context.net_id_by_name.find(net.name); existing != context.net_id_by_name.end()) {
      const auto& current = context.nets.at(existing->second);
      if (current.driver_node_id != net.driver_node_id || current.load_node_ids.size() != loads.size()
          || !std::ranges::all_of(current.load_node_ids, [&](auto load) -> bool { return loads.contains(load); })) {
        failure_reason = "clock_overlay_net_identity_conflict:" + net.name;
        return false;
      }
      continue;
    }
    const auto net_id = context.nets.size();
    for (const auto load : net.load_node_ids) {
      auto& node = context.nodes.at(load);
      if (node.incoming_net_id != kInvalidFastStaNetId) {
        failure_reason = "clock_overlay_load_has_multiple_nets:" + node.name;
        return false;
      }
      node.incoming_net_id = net_id;
    }
    context.nodes.at(net.driver_node_id).output_net_ids.push_back(net_id);
    if (!first_clock) {
      context.net_id_by_name.emplace(net.name, net_id);
    }
    context.nets.push_back(std::move(net));
  }
  if (first_clock) {
    context.net_id_by_name = std::move(clock_graph.net_id_by_name);
  }
  return true;
}

auto CollectClocks(const FastStaBuildInput& input) -> std::vector<const Clock*>
{
  std::vector<const Clock*> clocks;
  if (input.clock != nullptr) {
    clocks.push_back(input.clock);
  }
  if (input.committed_design != nullptr) {
    for (const auto* clock : input.committed_design->get_clocks()) {
      if (clock != nullptr && clock != input.clock) {
        clocks.push_back(clock);
      }
    }
  }
  return clocks;
}

auto CollectRoutes(const FastStaBuildInput& input) -> std::vector<FastStaClockRouteGeometry>
{
  std::vector<FastStaClockRouteGeometry> routes;
  if (input.committed_design != nullptr && input.committed_layout != nullptr) {
    for (std::size_t index = 0U; index < input.committed_design->get_clocks().size(); ++index) {
      routes.push_back(FastSTA::collectClockRouteGeometry(*input.committed_layout, index));
    }
  } else if (input.route_geometry != nullptr) {
    routes.push_back(*input.route_geometry);
  }
  return routes;
}

auto RoutesMatch(const std::vector<FastStaClockRouteGeometry>& lhs, const std::vector<FastStaClockRouteGeometry>& rhs) -> bool
{
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    const auto& first = lhs.at(index);
    const auto& second = rhs.at(index);
    if (first.design_dbu_per_um != second.design_dbu_per_um || first.clock_nets.size() != second.clock_nets.size()) {
      return false;
    }
    for (std::size_t net_index = 0U; net_index < first.clock_nets.size(); ++net_index) {
      const auto& first_net = first.clock_nets.at(net_index);
      const auto& second_net = second.clock_nets.at(net_index);
      if (first_net.net_name != second_net.net_name || first_net.routed_segments.size() != second_net.routed_segments.size()) {
        return false;
      }
      for (std::size_t segment_index = 0U; segment_index < first_net.routed_segments.size(); ++segment_index) {
        const auto& a = first_net.routed_segments.at(segment_index);
        const auto& b = second_net.routed_segments.at(segment_index);
        if (a.begin.x_dbu != b.begin.x_dbu || a.begin.y_dbu != b.begin.y_dbu || a.end.x_dbu != b.end.x_dbu || a.end.y_dbu != b.end.y_dbu
            || a.resistance_ohm != b.resistance_ohm || a.capacitance_pf != b.capacitance_pf || a.electrical_values_valid != b.electrical_values_valid) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace fast_sta

using fast_sta::AppendClock;
using fast_sta::CollectClocks;
using fast_sta::CollectRoutes;
using fast_sta::RoutesMatch;

auto FastStaBuilder::spliceClockContext(FastStaContext& context, FastStaContext overlay, const SdcClockData& constraints) -> std::optional<std::string>
{
  if (context.input_node_count > context.nodes.size() || context.input_net_count > context.nets.size() || context.liberty_revision != overlay.liberty_revision
      || context.wrapper != overlay.wrapper || context.dbu_per_um != overlay.dbu_per_um
      || context.owner_clock_scope_available != overlay.owner_clock_scope_available
      || (context.owner_clock_scope_available && (context.clock_name != overlay.clock_name || context.clock_net_name != overlay.clock_net_name))) {
    return "clock_topology_input_generation_mismatch";
  }
  const std::unordered_set<FastStaNetId> old_clock_nets(context.clock_overlay_net_ids.begin(), context.clock_overlay_net_ids.end());
  for (const auto id : context.clock_overlay_node_ids) {
    if (id < context.input_node_count && !overlay.node_id_by_name.contains(context.nodes.at(id).name)) {
      return "clock_topology_removed_input_pin:" + context.nodes.at(id).name;
    }
  }
  for (const auto id : context.clock_overlay_net_ids) {
    if (id < context.input_net_count && !overlay.net_id_by_name.contains(context.nets.at(id).name)) {
      return "clock_topology_removed_input_net:" + context.nets.at(id).name;
    }
  }
  for (const auto& node : overlay.nodes) {
    const auto existing = context.node_id_by_name.find(node.name);
    if (existing == context.node_id_by_name.end() || existing->second >= context.input_node_count) {
      continue;
    }
    const auto& previous = context.nodes.at(existing->second);
    if (previous.inst_name != node.inst_name || previous.location.x_dbu != node.location.x_dbu || previous.location.y_dbu != node.location.y_dbu) {
      return "clock_topology_changed_input_pin:" + node.name;
    }
    if (previous.cell_master != node.cell_master) {
      const auto from = context.liberty_cell_by_master.find(previous.cell_master);
      const auto to = overlay.liberty_cell_by_master.find(node.cell_master);
      if (from == context.liberty_cell_by_master.end() || to == overlay.liberty_cell_by_master.end() || from->second.input_port != to->second.input_port
          || from->second.output_port != to->second.output_port || from->second.timing_arc.positive_unate != to->second.timing_arc.positive_unate
          || from->second.timing_arc.negative_unate != to->second.timing_arc.negative_unate) {
        return "clock_topology_incompatible_input_master:" + node.name;
      }
    }
  }
  for (const auto& net : overlay.nets) {
    const auto existing = context.net_id_by_name.find(net.name);
    if (existing != context.net_id_by_name.end() && existing->second < context.input_net_count && !old_clock_nets.contains(existing->second)) {
      return "clock_topology_replaces_logic_net:" + net.name;
    }
  }

  // Detach only the previous physical overlay. Original signal nets and RC
  // stay in their resident slots, including backlinks outside the clock tree.
  for (const auto id : context.clock_overlay_net_ids) {
    const auto& net = context.nets.at(id);
    auto& outputs = context.nodes.at(net.driver_node_id).output_net_ids;
    std::erase(outputs, id);
    for (const auto load : net.load_node_ids) {
      if (context.nodes.at(load).incoming_net_id == id) {
        context.nodes.at(load).incoming_net_id = kInvalidFastStaNetId;
      }
    }
  }
  for (FastStaNodeId id = context.input_node_count; id < context.nodes.size(); ++id) {
    const auto& node = context.nodes.at(id);
    context.node_id_by_name.erase(node.name);
    const auto location = context.node_id_by_location.find({node.location.x_dbu, node.location.y_dbu});
    if (location != context.node_id_by_location.end() && location->second == id) {
      context.node_id_by_location.erase(location);
    }
  }
  for (FastStaNetId id = context.input_net_count; id < context.nets.size(); ++id) {
    context.net_id_by_name.erase(context.nets.at(id).name);
  }
  context.nodes.resize(context.input_node_count);
  context.nets.resize(context.input_net_count);
  context.initial_node_kinds.resize(context.input_node_count);
  context.initial_node_domains.resize(context.input_node_count);
  context.initial_net_domains.resize(context.input_net_count);
  std::erase_if(context.initial_buffer_inputs, [&](const auto& entry) -> bool { return entry.second >= context.input_node_count; });
  std::erase_if(context.initial_buffer_outputs, [&](const auto& entry) -> bool { return entry.second >= context.input_node_count; });
  std::vector<FastStaNodeId> node_ids;
  node_ids.reserve(overlay.nodes.size());
  for (const auto& source : overlay.nodes) {
    const auto [found, inserted] = context.node_id_by_name.try_emplace(source.name, context.nodes.size());
    const auto id = found->second;
    node_ids.push_back(id);
    auto node = source;
    node.incoming_net_id = kInvalidFastStaNetId;
    node.output_net_ids.clear();
    if (inserted) {
      context.initial_node_kinds.push_back(node.kind);
      context.initial_node_domains.push_back(node.domain);
      context.nodes.push_back(std::move(node));
      context.node_id_by_location.emplace(std::pair{source.location.x_dbu, source.location.y_dbu}, id);
    } else {
      auto& previous = context.nodes.at(id);
      node.incoming_net_id = previous.incoming_net_id;
      node.output_net_ids = std::move(previous.output_net_ids);
      node.top_level = previous.top_level;
      node.port_name = previous.port_name;
      node.input = previous.input;
      node.output = previous.output;
      node.clock_pin = previous.clock_pin;
      if (node.cell_master == previous.cell_master) {
        node.logic_function = previous.logic_function;
      }
      node.domain = context.initial_node_domains.at(id);
      context.initial_node_kinds.at(id) = node.kind;
      previous = std::move(node);
    }
    if (source.kind == FastStaNodeKind::kBufferInput) {
      context.initial_buffer_inputs[source.inst_name] = id;
    } else if (source.kind == FastStaNodeKind::kBufferOutput) {
      context.initial_buffer_outputs[source.inst_name] = id;
    }
  }
  const auto remap = [&](FastStaNodeId id) -> FastStaNodeId { return id < node_ids.size() ? node_ids.at(id) : kInvalidFastStaNodeId; };
  context.clock_overlay_net_ids.clear();
  for (auto& net : overlay.nets) {
    net.driver_node_id = remap(net.driver_node_id);
    for (auto& id : net.load_node_ids) {
      id = remap(id);
    }
    for (auto& rc : net.parasitic.rc_nodes) {
      rc.terminal_node_id = remap(rc.terminal_node_id);
      for (auto& id : rc.terminal_node_ids) {
        id = remap(id);
      }
    }
    const auto [found, inserted] = context.net_id_by_name.try_emplace(net.name, context.nets.size());
    const auto id = found->second;
    for (const auto load : net.load_node_ids) {
      auto& node = context.nodes.at(load);
      if (node.incoming_net_id != kInvalidFastStaNetId) {
        return "clock_topology_load_has_multiple_nets:" + node.name;
      }
      node.incoming_net_id = id;
    }
    context.nodes.at(net.driver_node_id).output_net_ids.push_back(id);
    if (inserted) {
      context.initial_net_domains.push_back(net.domain);
      context.nets.push_back(std::move(net));
    } else {
      context.initial_net_domains.at(id) = net.domain;
      context.nets.at(id) = std::move(net);
    }
    context.clock_overlay_net_ids.push_back(id);
  }
  context.clock_overlay_node_ids = std::move(node_ids);
  context.owned_clock_node_ids.clear();
  context.owned_clock_sink_node_ids.clear();
  for (const auto id : overlay.owned_clock_node_ids) {
    context.owned_clock_node_ids.push_back(context.clock_overlay_node_ids.at(id));
  }
  for (const auto id : overlay.owned_clock_sink_node_ids) {
    context.owned_clock_sink_node_ids.push_back(context.clock_overlay_node_ids.at(id));
  }
  context.owner_clock_scope_available = overlay.owner_clock_scope_available;
  context.source_node_id
      = overlay.source_node_id < context.clock_overlay_node_ids.size() ? context.clock_overlay_node_ids.at(overlay.source_node_id) : kInvalidFastStaNodeId;
  context.clock_period_ns = overlay.clock_period_ns;
  context.clock_routes = std::move(overlay.clock_routes);
  context.constraints = constraints;
  context.buffer_input_node_id_by_inst = context.initial_buffer_inputs;
  context.buffer_output_node_id_by_inst = context.initial_buffer_outputs;
  for (auto& [name, model] : overlay.liberty_cell_by_master) {
    (void) name;
    fast_sta::MergeLibertyCell(std::move(model), context);
  }
  for (auto& arc : context.timing_arcs) {
    arc.cell_master = context.nodes.at(arc.from_node_id).cell_master;
  }
  context.logic_preparation.reset();
  return std::nullopt;
}

auto FastStaBuilder::buildMatchingClockOverlay(const FastStaContext& context, const FastStaBuildInput& input) -> std::optional<MatchingClockOverlay>
{
  if ((input.constraints != nullptr && context.input_constraints != *input.constraints) || context.propagate_all_clocks != input.propagate_all_clocks
      || context.owner_clock_scope_available != (input.clock != nullptr)
      || (input.clock != nullptr
          && (context.clock_name != input.clock->get_clock_name() || context.clock_net_name != input.clock->get_clock_net_name()
              || context.clock_period_ns != input.clock->get_clock_period_ns() || context.source_node_id >= context.nodes.size()
              || context.nodes.at(context.source_node_id).name != Design::getPinFullName(input.clock->get_clock_source())))) {
    return std::nullopt;
  }
  MatchingClockOverlay matched;
  auto& overlay = matched.context;
  std::string reason;
  for (const auto* clock : CollectClocks(input)) {
    if (clock == nullptr || !AppendClock(*clock, clock == input.clock, overlay, reason)) {
      return std::nullopt;
    }
  }
  overlay.clock_routes = CollectRoutes(input);
  if (overlay.nodes.size() != context.clock_overlay_node_ids.size() || overlay.nets.size() != context.clock_overlay_net_ids.size()
      || !RoutesMatch(context.clock_routes, overlay.clock_routes)) {
    return std::nullopt;
  }
  matched.source_node_ids.reserve(overlay.nodes.size());
  matched.source_net_ids.reserve(overlay.nets.size());
  for (const auto& node : overlay.nodes) {
    const auto found = context.node_id_by_name.find(node.name);
    if (found == context.node_id_by_name.end()) {
      return std::nullopt;
    }
    const auto& current = context.nodes.at(found->second);
    if (node.inst_name != current.inst_name || node.cell_master != current.cell_master || node.location.x_dbu != current.location.x_dbu
        || node.location.y_dbu != current.location.y_dbu) {
      return std::nullopt;
    }
    matched.source_node_ids.push_back(found->second);
  }
  for (const auto& net : overlay.nets) {
    const auto found = context.net_id_by_name.find(net.name);
    if (found == context.net_id_by_name.end()) {
      return std::nullopt;
    }
    const auto& current = context.nets.at(found->second);
    if (current.load_node_ids.size() != net.load_node_ids.size()
        || context.nodes.at(current.driver_node_id).name != overlay.nodes.at(net.driver_node_id).name) {
      return std::nullopt;
    }
    for (std::size_t index = 0U; index < net.load_node_ids.size(); ++index) {
      if (context.nodes.at(current.load_node_ids.at(index)).name != overlay.nodes.at(net.load_node_ids.at(index)).name) {
        return std::nullopt;
      }
    }
    matched.source_net_ids.push_back(found->second);
  }
  return std::optional<MatchingClockOverlay>{std::move(matched)};
}

auto FastStaBuilder::matchesClockInput(const FastStaContext& context, const FastStaBuildInput& input) -> bool
{
  return buildMatchingClockOverlay(context, input).has_value();
}

auto FastStaBuilder::extractClockContext(const FastStaContext& source, const FastStaBuildInput& input) -> BuildResult
{
  if (!source.timing_valid) {
    return {.failure_reason = "clock_sizing_prepared_input_mismatch"};
  }
  auto overlay = buildMatchingClockOverlay(source, input);
  if (!overlay.has_value()) {
    return {.failure_reason = "clock_sizing_prepared_input_mismatch"};
  }
  // Validation has already constructed this exact clock topology.
  auto context = std::move(overlay->context);
  context.wrapper = source.wrapper;
  context.liberty_revision = source.liberty_revision;
  context.clock_name = input.clock == nullptr ? source.clock_name : input.clock->get_clock_name();
  context.clock_net_name = input.clock == nullptr ? source.clock_net_name : input.clock->get_clock_net_name();
  context.clock_period_ns = input.clock == nullptr ? source.clock_period_ns : input.clock->get_clock_period_ns();
  context.root_input_slew_ns = source.root_input_slew_ns;
  context.worker_count = source.worker_count;
  context.dbu_per_um = source.dbu_per_um;
  context.routing_layer = source.routing_layer;
  context.wire_width_um = source.wire_width_um;
  context.constraints = source.constraints;
  context.input_constraints = source.input_constraints;
  context.propagate_all_clocks = source.propagate_all_clocks;
  context.initial_node_kinds.reserve(context.nodes.size());
  context.initial_node_domains.reserve(context.nodes.size());
  context.clock_overlay_node_ids.reserve(context.nodes.size());
  context.initial_net_domains.reserve(context.nets.size());
  context.clock_overlay_net_ids.reserve(context.nets.size());
  context.initial_buffer_inputs.reserve(context.buffer_input_node_id_by_inst.size());
  context.initial_buffer_outputs.reserve(context.buffer_output_node_id_by_inst.size());
  const auto& source_node_ids = overlay->source_node_ids;
  std::unordered_map<FastStaNodeId, FastStaNodeId> local_node_ids;
  local_node_ids.reserve(context.nodes.size());
  for (FastStaNodeId id = 0U; id < context.nodes.size(); ++id) {
    local_node_ids.emplace(source_node_ids.at(id), id);
  }
  const auto remap_node = [&](FastStaNodeId id) -> FastStaNodeId {
    const auto found = local_node_ids.find(id);
    return found == local_node_ids.end() ? kInvalidFastStaNodeId : found->second;
  };
  const auto remap_point = [&](FastStaTimingPoint& point) -> void {
    point.launch_node_id = remap_node(point.launch_node_id);
    point.launch_clock_node_id = remap_node(point.launch_clock_node_id);
    point.stage_input_node_id = remap_node(point.stage_input_node_id);
  };
  for (FastStaNodeId id = 0U; id < context.nodes.size(); ++id) {
    auto& node = context.nodes.at(id);
    const auto source_id = source_node_ids.at(id);
    const auto incoming = node.incoming_net_id;
    auto outgoing = std::move(node.output_net_ids);
    node = source.nodes.at(source_id);
    node.incoming_net_id = incoming;
    node.output_net_ids = std::move(outgoing);
    remap_point(node.timing);
    for (std::size_t transition = 0U; transition < 2U; ++transition) {
      remap_point(node.early_timing.at(transition));
      remap_point(node.late_timing.at(transition));
      for (auto& point : node.tagged_early_timing.at(transition)) {
        remap_point(point);
      }
      for (auto& point : node.tagged_late_timing.at(transition)) {
        remap_point(point);
      }
    }
    if (const auto model = source.liberty_cell_by_master.find(node.cell_master); model != source.liberty_cell_by_master.end()) {
      context.liberty_cell_by_master.try_emplace(node.cell_master, model->second);
    }
    context.initial_node_kinds.push_back(source.initial_node_kinds.at(source_id));
    context.initial_node_domains.push_back(source.initial_node_domains.at(source_id));
    context.clock_overlay_node_ids.push_back(id);
    if (const auto caps = source.unconstrained_io_caps.find(source_id); caps != source.unconstrained_io_caps.end()) {
      context.unconstrained_io_caps.emplace(id, caps->second);
    }
  }
  for (FastStaNetId id = 0U; id < context.nets.size(); ++id) {
    auto& net = context.nets.at(id);
    const auto source_id = overlay->source_net_ids.at(id);
    net = source.nets.at(source_id);
    net.driver_node_id = remap_node(net.driver_node_id);
    for (auto& load : net.load_node_ids) {
      load = remap_node(load);
    }
    for (auto& node : net.parasitic.rc_nodes) {
      node.terminal_node_id = remap_node(node.terminal_node_id);
      for (auto& terminal : node.terminal_node_ids) {
        terminal = remap_node(terminal);
      }
    }
    context.initial_net_domains.push_back(source.initial_net_domains.at(source_id));
    context.clock_overlay_net_ids.push_back(id);
  }
  for (const auto id : source.clock_source_node_ids) {
    if (const auto mapped = remap_node(id); mapped != kInvalidFastStaNodeId) {
      context.clock_source_node_ids.push_back(mapped);
    }
  }
  for (const auto& [name, id] : source.initial_buffer_inputs) {
    if (const auto mapped = remap_node(id); mapped != kInvalidFastStaNodeId) {
      context.initial_buffer_inputs.emplace(name, mapped);
    }
  }
  for (const auto& [name, id] : source.initial_buffer_outputs) {
    if (const auto mapped = remap_node(id); mapped != kInvalidFastStaNodeId) {
      context.initial_buffer_outputs.emplace(name, mapped);
    }
  }
  context.case_values = source.case_values;
  context.source_node_id = remap_node(source.source_node_id);
  context.input_node_count = context.nodes.size();
  context.input_net_count = context.nets.size();
  context.timing_valid = true;
  context.clock_timing_valid = true;
  context.logic_tags_valid = true;
  context.timing_summary.status = FastStaTimingStatus::kComplete;
  return {.context = std::move(context), .failure_reason = {}};
}

}  // namespace icts
