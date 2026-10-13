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
 * @file FastSTAParasitics.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief RC network reduction for CTS timing.
 */

#include "FastSTAParasitics.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTAClockNetParasitic.hh"
#include "FastSTAClockState.hh"
#include "Logger.hh"
#include "design/Net.hh"
#include "io/Wrapper.hh"
#include "routing/SteinerTree.hh"

namespace icts {
namespace {

struct PiReductionResult
{
  double total_cap_pf = 0.0;
  FastStaPiModel pi;
  bool valid = false;
};

auto finiteNonnegative(double value) -> bool
{
  return std::isfinite(value) && value >= 0.0;
}

auto lateRisePinCapPf(const FastStaNode& node) -> double
{
  return node.input_cap_profile_available ? node.input_cap_pf_by_timing.at(1U).at(0U) : node.input_cap_pf;
}

auto rejectParasitic(FastStaContext& context, FastStaNet& net, const std::string& reason) -> bool
{
  net.parasitic.valid = false;
  net.load_rc_node_ids.clear();
  context.timing_valid = false;
  context.power_valid = false;
  context.timing_summary.status = FastStaTimingStatus::kInvalidInput;
  context.timing_summary.fallback_reason = "invalid_rc:" + net.name + ":" + reason;
  return false;
}

auto validateParasitic(const FastStaContext& context, const FastStaNet& net) -> std::optional<std::string>
{
  const auto& parasitic = net.parasitic;
  if (net.driver_node_id >= context.nodes.size() || parasitic.rc_nodes.empty() || parasitic.root_rc_node_id >= parasitic.rc_nodes.size()) {
    return "driver_or_root_unavailable";
  }
  std::unordered_set<FastStaNodeId> expected{net.driver_node_id};
  for (const auto load : net.load_node_ids) {
    if (load >= context.nodes.size() || !expected.insert(load).second) {
      return "load_identity_invalid_or_duplicated";
    }
  }
  for (const auto node_id : expected) {
    const auto& node = context.nodes.at(node_id);
    if (!finiteNonnegative(node.input_cap_pf)) {
      return "pin_capacitance_invalid:" + node.name;
    }
    if (node.input_cap_profile_available) {
      for (const auto& caps : node.input_cap_pf_by_timing) {
        if (!std::ranges::all_of(caps, finiteNonnegative)) {
          return "pin_capacitance_profile_invalid:" + node.name;
        }
      }
    }
  }
  std::unordered_map<FastStaNodeId, FastStaRcNodeId> terminal_locations;
  for (FastStaRcNodeId index = 0U; index < parasitic.rc_nodes.size(); ++index) {
    const auto& node = parasitic.rc_nodes.at(index);
    if (!finiteNonnegative(node.ground_cap_pf) || !finiteNonnegative(node.coupling_cap_pf) || !finiteNonnegative(node.wire_cap_pf)
        || !finiteNonnegative(node.driver_wire_cap_pf)) {
      return "wire_capacitance_invalid:" + node.name;
    }
    auto terminals = node.terminal_node_ids;
    if (node.terminal_node_id != kInvalidFastStaNodeId) {
      terminals.push_back(node.terminal_node_id);
    }
    for (const auto terminal : terminals) {
      if (!expected.contains(terminal)) {
        return "unexpected_terminal:" + node.name;
      }
      const auto [iter, inserted] = terminal_locations.emplace(terminal, index);
      if (!inserted && iter->second != index) {
        return "terminal_attached_to_multiple_rc_nodes";
      }
    }
  }
  if (terminal_locations.size() != expected.size() || terminal_locations.at(net.driver_node_id) != parasitic.root_rc_node_id) {
    return "terminal_missing_or_driver_not_root";
  }
  if (parasitic.pre_reduced_pi_elmore) {
    // An explicitly supplied Pi/terminal-Elmore model is not an RC edge graph.
    // It must nevertheless contain every real terminal and finite model facts.
    if (!finiteNonnegative(parasitic.pi.near_cap_pf) || !finiteNonnegative(parasitic.pi.far_cap_pf) || !finiteNonnegative(parasitic.pi.resistance_ohm)
        || !finiteNonnegative(parasitic.pi.near_cap_pf + parasitic.pi.far_cap_pf)
        || !std::ranges::all_of(parasitic.rc_nodes, [](const auto& node) -> bool { return finiteNonnegative(node.elmore_delay_ns); })) {
      return "pre_reduced_model_invalid";
    }
    return std::nullopt;
  }
  if (parasitic.rc_edges.size() != parasitic.rc_nodes.size() - 1U) {
    return "edge_count_not_a_tree";
  }
  std::vector<std::vector<FastStaRcNodeId>> adjacent(parasitic.rc_nodes.size());
  for (const auto& edge : parasitic.rc_edges) {
    if (edge.from >= adjacent.size() || edge.to >= adjacent.size() || edge.from == edge.to) {
      return "edge_endpoint_invalid";
    }
    if (!finiteNonnegative(edge.resistance_ohm) || !finiteNonnegative(edge.capacitance_pf) || !finiteNonnegative(edge.driver_capacitance_pf)
        || !finiteNonnegative(edge.ground_capacitance_pf) || !finiteNonnegative(edge.coupling_capacitance_pf)
        || !finiteNonnegative(edge.timing_coupling_factor)) {
      return "edge_electrical_value_invalid";
    }
    adjacent.at(edge.from).push_back(edge.to);
    adjacent.at(edge.to).push_back(edge.from);
  }
  std::vector<bool> seen(adjacent.size(), false);
  std::vector<FastStaRcNodeId> queue{parasitic.root_rc_node_id};
  seen.at(parasitic.root_rc_node_id) = true;
  for (std::size_t index = 0U; index < queue.size(); ++index) {
    for (const auto next : adjacent.at(queue.at(index))) {
      if (!seen.at(next)) {
        seen.at(next) = true;
        queue.push_back(next);
      }
    }
  }
  // A connected undirected graph with V-1 edges is a tree; this also rejects
  // parallel-edge cycles and disconnected islands without a traversal cutoff.
  return queue.size() == adjacent.size() ? std::nullopt : std::optional<std::string>{"disconnected_or_cyclic"};
}

auto buildAdjacency(const FastStaNetParasitic& parasitic) -> std::vector<std::vector<std::pair<FastStaRcNodeId, double>>>
{
  std::vector<std::vector<std::pair<FastStaRcNodeId, double>>> adjacency(parasitic.rc_nodes.size());
  for (const auto& edge : parasitic.rc_edges) {
    if (edge.from >= parasitic.rc_nodes.size() || edge.to >= parasitic.rc_nodes.size()) {
      continue;
    }
    adjacency.at(edge.from).emplace_back(edge.to, edge.resistance_ohm);
    adjacency.at(edge.to).emplace_back(edge.from, edge.resistance_ohm);
  }
  return adjacency;
}

auto makeRcNodeName(const std::string& net_name, const FastStaPoint& point) -> std::string
{
  return net_name + "@(" + std::to_string(point.x_dbu) + "," + std::to_string(point.y_dbu) + ")";
}

auto manhattanDistanceDbu(const FastStaPoint& lhs, const FastStaPoint& rhs) -> int
{
  const auto dx = std::abs(static_cast<int64_t>(lhs.x_dbu) - static_cast<int64_t>(rhs.x_dbu));
  const auto dy = std::abs(static_cast<int64_t>(lhs.y_dbu) - static_cast<int64_t>(rhs.y_dbu));
  const auto distance = dx + dy;
  return distance > static_cast<int64_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(distance);
}

auto wireLengthUm(const FastStaContext& context, int wire_distance_dbu) -> double
{
  if (context.dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "FastStaParasitics: DBU-per-micron is invalid.");
  }
  const auto dbu_per_um = context.dbu_per_um;
  return static_cast<double>(std::max(wire_distance_dbu, 0)) / static_cast<double>(dbu_per_um);
}

auto queryWireResistanceOhm(const FastStaContext& context, int wire_distance_dbu) -> double
{
  if (context.routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastStaParasitics: routing layer is invalid.");
  }
  const auto wirelength_um = wireLengthUm(context, wire_distance_dbu);
  if (wirelength_um <= 0.0) {
    return 0.0;
  }
  if (context.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastStaParasitics: Wrapper is unavailable.");
  }
  return context.wrapper->queryRequiredWireResistance(context.routing_layer, wirelength_um, context.wire_width_um);
}

auto queryWireCapacitanceProfile(const FastStaContext& context, int wire_distance_dbu) -> Wrapper::WireCapacitanceProfile
{
  if (context.routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastStaParasitics: routing layer is invalid.");
  }
  const auto wirelength_um = wireLengthUm(context, wire_distance_dbu);
  if (wirelength_um <= 0.0) {
    return {};
  }
  if (context.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastStaParasitics: Wrapper is unavailable.");
  }
  return context.wrapper->queryRequiredClockTimingWireCapacitanceProfile(context.routing_layer, wirelength_um, context.wire_width_um);
}

auto updateGroundCouplingTotals(FastStaNetParasitic& parasitic) -> void
{
  auto ground_cap_pf = 0.0;
  auto coupling_cap_pf = 0.0;
  auto weighted_factor_sum = 0.0;
  for (const auto& edge : parasitic.rc_edges) {
    const auto edge_ground_cap_pf = std::max(0.0, edge.ground_capacitance_pf);
    const auto edge_coupling_cap_pf = std::max(0.0, edge.coupling_capacitance_pf);
    ground_cap_pf += edge_ground_cap_pf;
    coupling_cap_pf += edge_coupling_cap_pf;
    weighted_factor_sum += edge_coupling_cap_pf * std::max(0.0, edge.timing_coupling_factor);
  }

  parasitic.ground_cap_pf = ground_cap_pf;
  parasitic.coupling_cap_pf = coupling_cap_pf;
  parasitic.timing_coupling_factor = coupling_cap_pf > 0.0 ? weighted_factor_sum / coupling_cap_pf : 0.0;
}

auto resetNetParasitic(FastStaNetParasitic& parasitic) -> void
{
  parasitic.rc_nodes.clear();
  parasitic.rc_edges.clear();
  parasitic.rc_node_id_by_name.clear();
  parasitic.root_rc_node_id = kInvalidFastStaRcNodeId;
  parasitic.pi = FastStaPiModel{};
  parasitic.driver_pi = FastStaPiModel{};
  parasitic.pi_by_timing = {};
  parasitic.driver_pi_by_timing = {};
  parasitic.ground_cap_pf = 0.0;
  parasitic.coupling_cap_pf = 0.0;
  parasitic.timing_coupling_factor = 0.0;
  parasitic.total_cap_pf = 0.0;
  parasitic.driver_total_cap_pf = 0.0;
  parasitic.pre_reduced_pi_elmore = false;
  parasitic.valid = false;
}

auto appendRcEdgeWithCapacitanceProfile(FastStaContext& context, FastStaNetParasitic& parasitic, FastStaRcNodeId from_id, FastStaRcNodeId to_id,
                                        int distance_dbu) -> void
{
  const auto resistance_ohm = queryWireResistanceOhm(context, distance_dbu);
  const auto capacitance_profile = queryWireCapacitanceProfile(context, distance_dbu);
  const auto capacitance_pf = capacitance_profile.total_cap_pf;
  const auto driver_capacitance_pf = capacitance_profile.timing_effective_cap_pf;
  parasitic.rc_edges.push_back(FastStaRcEdge{
      .from = from_id,
      .to = to_id,
      .resistance_ohm = resistance_ohm,
      .ground_capacitance_pf = capacitance_profile.ground_cap_pf,
      .coupling_capacitance_pf = capacitance_profile.coupling_cap_pf,
      .capacitance_pf = capacitance_pf,
      .timing_coupling_factor = capacitance_profile.timing_coupling_factor,
      .driver_capacitance_pf = driver_capacitance_pf,
  });
  parasitic.rc_nodes.at(from_id).ground_cap_pf += capacitance_profile.ground_cap_pf / 2.0;
  parasitic.rc_nodes.at(to_id).ground_cap_pf += capacitance_profile.ground_cap_pf / 2.0;
  parasitic.rc_nodes.at(from_id).coupling_cap_pf += capacitance_profile.coupling_cap_pf / 2.0;
  parasitic.rc_nodes.at(to_id).coupling_cap_pf += capacitance_profile.coupling_cap_pf / 2.0;
  parasitic.rc_nodes.at(from_id).wire_cap_pf += capacitance_pf / 2.0;
  parasitic.rc_nodes.at(to_id).wire_cap_pf += capacitance_pf / 2.0;
  parasitic.rc_nodes.at(from_id).driver_wire_cap_pf += driver_capacitance_pf / 2.0;
  parasitic.rc_nodes.at(to_id).driver_wire_cap_pf += driver_capacitance_pf / 2.0;
}

auto appendRcEdgeWithElectricalValues(FastStaNetParasitic& parasitic, FastStaRcNodeId from_id, FastStaRcNodeId to_id, double resistance_ohm,
                                      double capacitance_pf) -> void
{
  const auto cap_pf = capacitance_pf;
  parasitic.rc_edges.push_back(FastStaRcEdge{
      .from = from_id,
      .to = to_id,
      .resistance_ohm = resistance_ohm,
      .ground_capacitance_pf = cap_pf,
      .coupling_capacitance_pf = 0.0,
      .capacitance_pf = cap_pf,
      .timing_coupling_factor = 0.0,
      .driver_capacitance_pf = cap_pf,
  });
  parasitic.rc_nodes.at(from_id).ground_cap_pf += cap_pf / 2.0;
  parasitic.rc_nodes.at(to_id).ground_cap_pf += cap_pf / 2.0;
  parasitic.rc_nodes.at(from_id).wire_cap_pf += cap_pf / 2.0;
  parasitic.rc_nodes.at(to_id).wire_cap_pf += cap_pf / 2.0;
  parasitic.rc_nodes.at(from_id).driver_wire_cap_pf += cap_pf / 2.0;
  parasitic.rc_nodes.at(to_id).driver_wire_cap_pf += cap_pf / 2.0;
}

auto appendRcNode(FastStaContext& context, FastStaNet& net, const FastStaPoint& point) -> FastStaRcNodeId
{
  (void) context;
  auto& parasitic = net.parasitic;
  const auto name = makeRcNodeName(net.name, point);
  if (const auto iter = parasitic.rc_node_id_by_name.find(name); iter != parasitic.rc_node_id_by_name.end()) {
    return iter->second;
  }

  const auto rc_node_id = parasitic.rc_nodes.size();
  parasitic.rc_node_id_by_name[name] = rc_node_id;
  parasitic.rc_nodes.push_back(FastStaRcNode{
      .name = name,
      .terminal_node_id = kInvalidFastStaNodeId,
      .terminal_node_ids = {},
  });
  return rc_node_id;
}

auto attachNetTerminals(FastStaContext& context, FastStaNet& net) -> bool
{
  const auto attach = [&](FastStaNodeId node_id) -> bool {
    if (node_id >= context.nodes.size()) {
      return false;
    }
    const auto name = makeRcNodeName(net.name, context.nodes.at(node_id).location);
    const auto rc_iter = net.parasitic.rc_node_id_by_name.find(name);
    if (rc_iter == net.parasitic.rc_node_id_by_name.end() || rc_iter->second >= net.parasitic.rc_nodes.size()) {
      return false;
    }
    auto& rc_node = net.parasitic.rc_nodes.at(rc_iter->second);
    if (rc_node.terminal_node_id == kInvalidFastStaNodeId) {
      rc_node.terminal_node_id = node_id;
    }
    if (std::ranges::find(rc_node.terminal_node_ids, node_id) == rc_node.terminal_node_ids.end()) {
      rc_node.terminal_node_ids.push_back(node_id);
    }
    return true;
  };
  if (!attach(net.driver_node_id)) {
    return false;
  }
  return std::ranges::all_of(net.load_node_ids, attach);
}

auto indexNetLoadRcNodes(FastStaNet& net) -> void
{
  std::unordered_map<FastStaNodeId, FastStaRcNodeId> rc_node_by_terminal;
  for (FastStaRcNodeId rc_node_id = 0U; rc_node_id < net.parasitic.rc_nodes.size(); ++rc_node_id) {
    const auto& rc_node = net.parasitic.rc_nodes.at(rc_node_id);
    if (rc_node.terminal_node_id != kInvalidFastStaNodeId) {
      rc_node_by_terminal.try_emplace(rc_node.terminal_node_id, rc_node_id);
    }
    for (const auto terminal_node_id : rc_node.terminal_node_ids) {
      rc_node_by_terminal.try_emplace(terminal_node_id, rc_node_id);
    }
  }
  net.load_rc_node_ids.clear();
  net.load_rc_node_ids.reserve(net.load_node_ids.size());
  for (const auto load_node_id : net.load_node_ids) {
    const auto iter = rc_node_by_terminal.find(load_node_id);
    net.load_rc_node_ids.push_back(iter == rc_node_by_terminal.end() ? kInvalidFastStaRcNodeId : iter->second);
  }
}

auto allNetTerminalsCoincident(const FastStaContext& context, const FastStaNet& net) -> bool
{
  if (net.driver_node_id >= context.nodes.size()) {
    return false;
  }
  const auto location = context.nodes.at(net.driver_node_id).location;
  return std::ranges::all_of(net.load_node_ids, [&](FastStaNodeId node_id) -> bool {
    return node_id < context.nodes.size() && context.nodes.at(node_id).location.x_dbu == location.x_dbu
           && context.nodes.at(node_id).location.y_dbu == location.y_dbu;
  });
}

auto routedEdgeDistanceDbu(const ClockSteinerTree<int>::EdgeType& edge) -> int
{
  return std::max(edge.distance, edge.routed_distance);
}

auto updateNetLoad(FastStaContext& context, FastStaNet& net) -> void
{
  if (!net.parasitic.rc_nodes.empty()) {
    auto& parasitic = net.parasitic;
    auto load_cap_pf = 0.0;
    std::unordered_set<FastStaNodeId> rc_terminal_nodes;
    for (auto& rc_node : parasitic.rc_nodes) {
      rc_node.pin_cap_pf = 0.0;
      std::unordered_set<FastStaNodeId> terminals(rc_node.terminal_node_ids.begin(), rc_node.terminal_node_ids.end());
      if (rc_node.terminal_node_id != kInvalidFastStaNodeId) {
        terminals.insert(rc_node.terminal_node_id);
      }
      for (const auto terminal_node_id : terminals) {
        if (terminal_node_id < context.nodes.size()) {
          rc_node.pin_cap_pf += lateRisePinCapPf(context.nodes.at(terminal_node_id));
          rc_terminal_nodes.insert(terminal_node_id);
        }
      }
      rc_node.cap_pf = rc_node.wire_cap_pf + rc_node.pin_cap_pf;
      rc_node.driver_cap_pf = rc_node.driver_wire_cap_pf + rc_node.pin_cap_pf;
      load_cap_pf += rc_node.cap_pf;
    }
    for (const auto load_node_id : net.load_node_ids) {
      if (load_node_id < context.nodes.size() && !rc_terminal_nodes.contains(load_node_id)) {
        load_cap_pf += lateRisePinCapPf(context.nodes.at(load_node_id));
      }
    }
    parasitic.total_cap_pf = load_cap_pf;
    updateGroundCouplingTotals(parasitic);
    net.load_cap_pf = load_cap_pf;
    return;
  }

  auto load_cap_pf = 0.0;
  for (const auto load_node_id : net.load_node_ids) {
    if (load_node_id >= context.nodes.size()) {
      continue;
    }
    load_cap_pf += lateRisePinCapPf(context.nodes.at(load_node_id));
  }
  load_cap_pf += net.wire_cap_pf;
  net.load_cap_pf = load_cap_pf;
}

auto timingNodeCapPf(const FastStaContext& context, const FastStaRcNode& rc_node, bool driver_cap, std::size_t analysis_index, std::size_t transition_index)
    -> double
{
  auto cap_pf = std::max(0.0, driver_cap ? rc_node.driver_wire_cap_pf : rc_node.wire_cap_pf);
  std::unordered_set<FastStaNodeId> terminals(rc_node.terminal_node_ids.begin(), rc_node.terminal_node_ids.end());
  if (rc_node.terminal_node_id != kInvalidFastStaNodeId) {
    terminals.insert(rc_node.terminal_node_id);
  }
  for (const auto terminal_node_id : terminals) {
    if (terminal_node_id >= context.nodes.size()) {
      continue;
    }
    const auto& node = context.nodes.at(terminal_node_id);
    const auto profiled_cap = node.input_cap_pf_by_timing.at(analysis_index).at(transition_index);
    cap_pf += std::max(0.0, node.input_cap_profile_available ? profiled_cap : node.input_cap_pf);
  }
  return cap_pf;
}

}  // namespace

auto FastStaParasitics::updateNetLoads(FastStaContext& context) -> void
{
  for (auto& net : context.nets) {
    updateNetLoad(context, net);
  }
}

auto FastStaParasitics::updateNetLoads(FastStaContext& context, const std::vector<FastStaNetId>& net_ids) -> void
{
  for (const auto net_id : net_ids) {
    if (net_id >= context.nets.size()) {
      continue;
    }
    auto& net = context.nets.at(net_id);
    updateNetLoad(context, net);
  }
}

auto FastStaParasitics::buildNetParasiticFromSegments(FastStaContext& context, FastStaNetId net_id, const std::vector<FastStaRcSegment>& segments) -> bool
{
  if (net_id >= context.nets.size()) {
    return false;
  }
  auto& net = context.nets.at(net_id);
  auto& parasitic = net.parasitic;
  resetNetParasitic(parasitic);
  net.load_rc_node_ids.clear();
  context.timing_valid = false;
  context.power_valid = false;

  if (segments.empty()) {
    if (!allNetTerminalsCoincident(context, net)) {
      return rejectParasitic(context, net, "segments_missing_for_noncoincident_terminals");
    }
    parasitic.root_rc_node_id = appendRcNode(context, net, context.nodes.at(net.driver_node_id).location);
    if (!attachNetTerminals(context, net)) {
      resetNetParasitic(parasitic);
      return rejectParasitic(context, net, "terminal_attachment_failed");
    }
    if (const auto issue = validateParasitic(context, net); issue.has_value()) {
      return rejectParasitic(context, net, *issue);
    }
    return true;
  }

  for (const auto& segment : segments) {
    if (segment.electrical_values_valid && (!finiteNonnegative(segment.resistance_ohm) || !finiteNonnegative(segment.capacitance_pf))) {
      return rejectParasitic(context, net, "segment_electrical_value_invalid");
    }
    const auto from_id = appendRcNode(context, net, segment.begin);
    const auto to_id = appendRcNode(context, net, segment.end);
    if (from_id == to_id) {
      if (segment.electrical_values_valid && (segment.resistance_ohm != 0.0 || segment.capacitance_pf != 0.0)) {
        return rejectParasitic(context, net, "self_loop_has_electrical_values");
      }
      continue;
    }
    const auto distance_dbu = manhattanDistanceDbu(segment.begin, segment.end);
    if (segment.electrical_values_valid) {
      appendRcEdgeWithElectricalValues(parasitic, from_id, to_id, segment.resistance_ohm, segment.capacitance_pf);
    } else {
      appendRcEdgeWithCapacitanceProfile(context, parasitic, from_id, to_id, distance_dbu);
    }
  }

  if (net.driver_node_id != kInvalidFastStaNodeId && net.driver_node_id < context.nodes.size()) {
    const auto driver_key = makeRcNodeName(net.name, context.nodes.at(net.driver_node_id).location);
    if (const auto iter = parasitic.rc_node_id_by_name.find(driver_key); iter != parasitic.rc_node_id_by_name.end()) {
      parasitic.root_rc_node_id = iter->second;
    }
  }
  if (parasitic.root_rc_node_id == kInvalidFastStaRcNodeId) {
    return rejectParasitic(context, net, "driver_not_in_segments");
  }
  if (!attachNetTerminals(context, net)) {
    resetNetParasitic(parasitic);
    return rejectParasitic(context, net, "terminal_attachment_failed");
  }
  if (const auto issue = validateParasitic(context, net); issue.has_value()) {
    return rejectParasitic(context, net, *issue);
  }
  updateGroundCouplingTotals(parasitic);
  return true;
}

auto FastStaParasitics::buildNetParasiticFromRouteTree(FastStaContext& context, FastStaNetId net_id, const Net& net, const ClockSteinerTree<int>& route_tree)
    -> bool
{
  if (net_id >= context.nets.size() || route_tree.node_count() == 0 || route_tree.edge_count() == 0) {
    return false;
  }
  if (!route_tree.validate()) {
    return false;
  }
  auto& fast_net = context.nets.at(net_id);
  if (fast_net.name != net.get_name()) {
    return false;
  }

  auto& parasitic = fast_net.parasitic;
  resetNetParasitic(parasitic);
  fast_net.load_rc_node_ids.clear();
  context.timing_valid = false;
  context.power_valid = false;

  std::vector<FastStaRcNodeId> rc_node_by_route_node(route_tree.node_count(), kInvalidFastStaRcNodeId);
  for (const auto& route_node : route_tree.get_nodes()) {
    if (route_node.id >= rc_node_by_route_node.size()) {
      return rejectParasitic(context, fast_net, "route_node_id_invalid");
    }

    auto terminal_node_id = kInvalidFastStaNodeId;
    if (route_node.is_terminal) {
      const auto terminal_iter = context.node_id_by_name.find(route_node.name);
      if (terminal_iter == context.node_id_by_name.end() || terminal_iter->second >= context.nodes.size()) {
        return rejectParasitic(context, fast_net, "route_terminal_unresolved:" + route_node.name);
      }
      terminal_node_id = terminal_iter->second;
    }

    const auto rc_node_id = parasitic.rc_nodes.size();
    rc_node_by_route_node.at(route_node.id) = rc_node_id;
    parasitic.rc_node_id_by_name[route_node.name] = rc_node_id;
    parasitic.rc_nodes.push_back(FastStaRcNode{
        .name = route_node.name,
        .terminal_node_id = terminal_node_id,
        .terminal_node_ids = terminal_node_id == kInvalidFastStaNodeId ? std::vector<FastStaNodeId>{} : std::vector<FastStaNodeId>{terminal_node_id},
    });
  }

  for (const auto& route_edge : route_tree.get_edges()) {
    if (route_edge.source_node_id >= rc_node_by_route_node.size() || route_edge.target_node_id >= rc_node_by_route_node.size()) {
      return rejectParasitic(context, fast_net, "route_edge_endpoint_invalid");
    }
    const auto from_id = rc_node_by_route_node.at(route_edge.source_node_id);
    const auto to_id = rc_node_by_route_node.at(route_edge.target_node_id);
    if (from_id == kInvalidFastStaRcNodeId || to_id == kInvalidFastStaRcNodeId || from_id == to_id) {
      return rejectParasitic(context, fast_net, "route_edge_identity_invalid");
    }

    const auto distance_dbu = routedEdgeDistanceDbu(route_edge);
    appendRcEdgeWithCapacitanceProfile(context, parasitic, from_id, to_id, distance_dbu);
  }

  const auto root_route_node_id = route_tree.get_root();
  if (root_route_node_id >= rc_node_by_route_node.size()) {
    return rejectParasitic(context, fast_net, "route_root_invalid");
  }
  parasitic.root_rc_node_id = rc_node_by_route_node.at(root_route_node_id);
  if (const auto issue = validateParasitic(context, fast_net); issue.has_value()) {
    return rejectParasitic(context, fast_net, *issue);
  }
  updateGroundCouplingTotals(parasitic);
  updateNetLoad(context, fast_net);
  return true;
}

auto FastStaParasitics::reduceToPiElmore(FastStaContext& context, FastStaNetId net_id) -> bool
{
  if (net_id >= context.nets.size()) {
    return false;
  }
  auto& net = context.nets.at(net_id);
  auto& parasitic = net.parasitic;
  parasitic.valid = false;
  // An absent RC graph is only a legitimate zero-wire model when every real
  // terminal is physically coincident. Never make missing geometry zero-delay.
  if (parasitic.rc_nodes.empty() && !parasitic.pre_reduced_pi_elmore) {
    if (!buildNetParasiticFromSegments(context, net_id, {})) {
      return false;
    }
  }
  if (const auto issue = validateParasitic(context, net); issue.has_value()) {
    return rejectParasitic(context, net, *issue);
  }
  if (parasitic.pre_reduced_pi_elmore) {
    auto total_cap_pf = 0.0;
    auto driver_total_cap_pf = 0.0;
    for (auto& rc_node : parasitic.rc_nodes) {
      for (auto& delays : rc_node.elmore_delay_ns_by_timing) {
        delays.fill(rc_node.elmore_delay_ns);
      }
      rc_node.pin_cap_pf = 0.0;
      std::unordered_set<FastStaNodeId> terminals(rc_node.terminal_node_ids.begin(), rc_node.terminal_node_ids.end());
      if (rc_node.terminal_node_id != kInvalidFastStaNodeId) {
        terminals.insert(rc_node.terminal_node_id);
      }
      for (const auto terminal_node_id : terminals) {
        if (terminal_node_id < context.nodes.size()) {
          rc_node.pin_cap_pf += lateRisePinCapPf(context.nodes.at(terminal_node_id));
        }
      }
      rc_node.cap_pf = rc_node.wire_cap_pf + rc_node.pin_cap_pf;
      rc_node.driver_cap_pf = rc_node.driver_wire_cap_pf + rc_node.pin_cap_pf;
      if (!finiteNonnegative(rc_node.cap_pf) || !finiteNonnegative(rc_node.driver_cap_pf)) {
        return rejectParasitic(context, net, "pre_reduced_capacitance_overflow");
      }
      total_cap_pf += rc_node.cap_pf;
      driver_total_cap_pf += rc_node.driver_cap_pf;
    }
    if (!finiteNonnegative(total_cap_pf) || !finiteNonnegative(driver_total_cap_pf)) {
      return rejectParasitic(context, net, "pre_reduced_total_capacitance_overflow");
    }
    parasitic.total_cap_pf = total_cap_pf;
    parasitic.driver_total_cap_pf = driver_total_cap_pf;
    parasitic.driver_pi = parasitic.pi;
    for (auto& models : parasitic.pi_by_timing) {
      models.fill(parasitic.pi);
    }
    for (auto& models : parasitic.driver_pi_by_timing) {
      models.fill(parasitic.driver_pi);
    }
    updateGroundCouplingTotals(parasitic);
    net.load_cap_pf = total_cap_pf;
    parasitic.valid = true;
    indexNetLoadRcNodes(net);
    return true;
  }
  const auto adjacency = buildAdjacency(parasitic);
  std::vector<FastStaRcNodeId> parents(parasitic.rc_nodes.size(), kInvalidFastStaRcNodeId);
  std::vector<double> parent_resistances(parasitic.rc_nodes.size(), 0.0);
  std::vector<FastStaRcNodeId> traversal{parasitic.root_rc_node_id};
  parents.at(parasitic.root_rc_node_id) = parasitic.root_rc_node_id;
  for (std::size_t index = 0U; index < traversal.size(); ++index) {
    const auto node_id = traversal.at(index);
    for (const auto& [next_id, resistance] : adjacency.at(node_id)) {
      if (parents.at(next_id) == kInvalidFastStaRcNodeId) {
        parents.at(next_id) = node_id;
        parent_resistances.at(next_id) = resistance;
        traversal.push_back(next_id);
      }
    }
  }

  struct MomentState
  {
    double y1 = 0.0;
    double y2 = 0.0;
    double y3 = 0.0;
    double downstream_cap = 0.0;
  };

  auto reduce_pi_model
      = [&](bool use_driver_cap, std::size_t analysis_index, std::size_t transition_index, std::vector<double>& downstream_by_node) -> PiReductionResult {
    downstream_by_node.assign(parasitic.rc_nodes.size(), 0.0);
    std::vector<MomentState> moments(parasitic.rc_nodes.size());
    for (const auto node_id : traversal | std::views::reverse) {
      auto& node = parasitic.rc_nodes.at(node_id);
      const auto node_cap_pf = timingNodeCapPf(context, node, use_driver_cap, analysis_index, transition_index);
      MomentState state{
          .y1 = node_cap_pf,
          .y2 = 0.0,
          .y3 = 0.0,
          .downstream_cap = node_cap_pf,
      };

      for (const auto& [child_id, resistance_ohm] : adjacency.at(node_id)) {
        if (parents.at(child_id) != node_id || child_id == node_id) {
          continue;
        }
        const auto& child_state = moments.at(child_id);
        state.y1 += child_state.y1;
        state.y2 += child_state.y2 - resistance_ohm * child_state.y1 * child_state.y1;
        state.y3 += child_state.y3 - 2.0 * resistance_ohm * child_state.y1 * child_state.y2
                    + resistance_ohm * resistance_ohm * child_state.y1 * child_state.y1 * child_state.y1;
        state.downstream_cap += child_state.downstream_cap;
      }
      if (!finiteNonnegative(state.y1) || !std::isfinite(state.y2) || !std::isfinite(state.y3) || !finiteNonnegative(state.downstream_cap)) {
        return PiReductionResult{};
      }
      downstream_by_node.at(node_id) = state.downstream_cap;
      moments.at(node_id) = state;
    }

    const auto& root_state = moments.at(parasitic.root_rc_node_id);
    PiReductionResult result{.total_cap_pf = root_state.y1, .pi = {}, .valid = true};
    if (std::abs(root_state.y2) <= 1e-18 || std::abs(root_state.y3) <= 1e-18) {
      result.pi.near_cap_pf = result.total_cap_pf;
      result.pi.far_cap_pf = 0.0;
      result.pi.resistance_ohm = 0.0;
      return result;
    }

    const auto c1 = root_state.y2 * root_state.y2 / root_state.y3;
    const auto c2 = root_state.y1 - c1;
    const auto rpi = -root_state.y3 * root_state.y3 / (root_state.y2 * root_state.y2 * root_state.y2);
    if (!std::isfinite(c1) || !std::isfinite(c2) || !std::isfinite(rpi)) {
      return PiReductionResult{};
    }
    result.pi.near_cap_pf = std::max(0.0, c2);
    result.pi.far_cap_pf = std::max(0.0, c1);
    result.pi.resistance_ohm = std::max(0.0, rpi);
    return result;
  };

  for (std::size_t analysis_index = 0U; analysis_index < 2U; ++analysis_index) {
    for (std::size_t transition_index = 0U; transition_index < 2U; ++transition_index) {
      std::vector<double> downstream_by_node;
      const auto physical_result = reduce_pi_model(false, analysis_index, transition_index, downstream_by_node);
      if (!physical_result.valid) {
        return rejectParasitic(context, net, "pi_moment_numeric_overflow");
      }
      parasitic.pi_by_timing.at(analysis_index).at(transition_index) = physical_result.pi;
      for (const auto node_id : traversal) {
        auto& elmore = parasitic.rc_nodes.at(node_id).elmore_delay_ns_by_timing.at(analysis_index).at(transition_index);
        elmore = node_id == parasitic.root_rc_node_id
                     ? 0.0
                     : parasitic.rc_nodes.at(parents.at(node_id)).elmore_delay_ns_by_timing.at(analysis_index).at(transition_index)
                           + parent_resistances.at(node_id) * downstream_by_node.at(node_id) * 1e-3;
        if (!finiteNonnegative(elmore)) {
          return rejectParasitic(context, net, "elmore_numeric_overflow");
        }
      }

      std::vector<double> driver_downstream_by_node;
      const auto driver_result = reduce_pi_model(true, analysis_index, transition_index, driver_downstream_by_node);
      if (!driver_result.valid) {
        return rejectParasitic(context, net, "driver_pi_moment_numeric_overflow");
      }
      parasitic.driver_pi_by_timing.at(analysis_index).at(transition_index) = driver_result.pi;
      if (analysis_index == 1U && transition_index == 0U) {
        parasitic.total_cap_pf = physical_result.total_cap_pf;
        parasitic.pi = physical_result.pi;
        parasitic.driver_total_cap_pf = driver_result.total_cap_pf;
        parasitic.driver_pi = driver_result.pi;
        for (std::size_t node_id = 0U; node_id < parasitic.rc_nodes.size(); ++node_id) {
          auto& node = parasitic.rc_nodes.at(node_id);
          node.downstream_cap_pf = downstream_by_node.at(node_id);
          node.driver_downstream_cap_pf = driver_downstream_by_node.at(node_id);
          node.elmore_delay_ns = node.elmore_delay_ns_by_timing.at(analysis_index).at(transition_index);
        }
      }
    }
  }
  updateGroundCouplingTotals(parasitic);
  net.load_cap_pf = parasitic.total_cap_pf;
  parasitic.valid = true;
  indexNetLoadRcNodes(net);
  return true;
}

}  // namespace icts
