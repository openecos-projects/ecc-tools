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
 * @brief OpenSTA-style RC reduction data path implementation for CTS fast STA.
 */

#include "FastSTAParasitics.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
};

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

auto makeLocationKey(const FastStaPoint& point) -> std::pair<int, int>
{
  return {point.x_dbu, point.y_dbu};
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

auto wireLengthUm(const FastStaClockContext& context, int wire_distance_dbu) -> double
{
  if (context.dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "FastStaParasitics: DBU-per-micron is invalid.");
  }
  const auto dbu_per_um = context.dbu_per_um;
  return static_cast<double>(std::max(wire_distance_dbu, 0)) / static_cast<double>(dbu_per_um);
}

auto queryWireResistanceOhm(const FastStaClockContext& context, int wire_distance_dbu) -> double
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

auto queryWireCapacitanceProfile(const FastStaClockContext& context, int wire_distance_dbu) -> Wrapper::WireCapacitanceProfile
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
  parasitic.ground_cap_pf = 0.0;
  parasitic.coupling_cap_pf = 0.0;
  parasitic.timing_coupling_factor = 0.0;
  parasitic.total_cap_pf = 0.0;
  parasitic.driver_total_cap_pf = 0.0;
  parasitic.valid = false;
}

auto appendRcEdgeWithCapacitanceProfile(FastStaClockContext& context, FastStaNetParasitic& parasitic, FastStaRcNodeId from_id, FastStaRcNodeId to_id,
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

auto appendRcNode(FastStaClockContext& context, FastStaNet& net, const FastStaPoint& point) -> FastStaRcNodeId
{
  auto& parasitic = net.parasitic;
  const auto name = makeRcNodeName(net.name, point);
  if (const auto iter = parasitic.rc_node_id_by_name.find(name); iter != parasitic.rc_node_id_by_name.end()) {
    return iter->second;
  }

  auto terminal_node_id = kInvalidFastStaNodeId;
  if (const auto terminal_iter = context.node_id_by_location.find(makeLocationKey(point)); terminal_iter != context.node_id_by_location.end()) {
    terminal_node_id = terminal_iter->second;
  }
  const auto rc_node_id = parasitic.rc_nodes.size();
  parasitic.rc_node_id_by_name[name] = rc_node_id;
  parasitic.rc_nodes.push_back(FastStaRcNode{
      .name = name,
      .terminal_node_id = terminal_node_id,
  });
  return rc_node_id;
}

auto routedEdgeDistanceDbu(const ClockSteinerTree<int>::EdgeType& edge) -> int
{
  return std::max(edge.distance, edge.routed_distance);
}

auto updateNetLoad(FastStaClockContext& context, FastStaNet& net) -> void
{
  if (!net.parasitic.rc_nodes.empty()) {
    auto& parasitic = net.parasitic;
    auto load_cap_pf = 0.0;
    std::unordered_set<FastStaNodeId> rc_terminal_nodes;
    for (auto& rc_node : parasitic.rc_nodes) {
      rc_node.ground_cap_pf = std::max(0.0, rc_node.ground_cap_pf);
      rc_node.coupling_cap_pf = std::max(0.0, rc_node.coupling_cap_pf);
      rc_node.wire_cap_pf = std::max(0.0, rc_node.wire_cap_pf);
      rc_node.pin_cap_pf = 0.0;
      if (rc_node.terminal_node_id != kInvalidFastStaNodeId && rc_node.terminal_node_id < context.nodes.size()) {
        rc_node.pin_cap_pf = std::max(0.0, context.nodes.at(rc_node.terminal_node_id).input_cap_pf);
        rc_terminal_nodes.insert(rc_node.terminal_node_id);
      }
      rc_node.cap_pf = rc_node.wire_cap_pf + rc_node.pin_cap_pf;
      rc_node.driver_cap_pf = std::max(0.0, rc_node.driver_wire_cap_pf) + rc_node.pin_cap_pf;
      load_cap_pf += rc_node.cap_pf;
    }
    for (const auto load_node_id : net.load_node_ids) {
      if (load_node_id < context.nodes.size() && !rc_terminal_nodes.contains(load_node_id)) {
        load_cap_pf += std::max(0.0, context.nodes.at(load_node_id).input_cap_pf);
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
    load_cap_pf += std::max(0.0, context.nodes.at(load_node_id).input_cap_pf);
  }
  load_cap_pf += std::max(0.0, net.wire_cap_pf);
  net.load_cap_pf = load_cap_pf;
}

}  // namespace

auto FastStaParasitics::updateNetLoads(FastStaClockContext& context) -> void
{
  for (auto& net : context.nets) {
    updateNetLoad(context, net);
  }
}

auto FastStaParasitics::updateNetLoads(FastStaClockContext& context, const std::vector<FastStaNetId>& net_ids) -> void
{
  for (const auto net_id : net_ids) {
    if (net_id >= context.nets.size()) {
      continue;
    }
    auto& net = context.nets.at(net_id);
    updateNetLoad(context, net);
  }
}

auto FastStaParasitics::buildNetParasiticFromSegments(FastStaClockContext& context, FastStaNetId net_id, const std::vector<FastStaRcSegment>& segments) -> bool
{
  if (net_id >= context.nets.size()) {
    return false;
  }
  auto& net = context.nets.at(net_id);
  auto& parasitic = net.parasitic;
  resetNetParasitic(parasitic);

  if (segments.empty()) {
    return false;
  }

  for (const auto& segment : segments) {
    const auto from_id = appendRcNode(context, net, segment.begin);
    const auto to_id = appendRcNode(context, net, segment.end);
    if (from_id == kInvalidFastStaRcNodeId || to_id == kInvalidFastStaRcNodeId || from_id == to_id) {
      continue;
    }
    const auto distance_dbu = manhattanDistanceDbu(segment.begin, segment.end);
    appendRcEdgeWithCapacitanceProfile(context, parasitic, from_id, to_id, distance_dbu);
  }

  if (parasitic.rc_nodes.empty() || parasitic.rc_edges.empty()) {
    parasitic.rc_nodes.clear();
    parasitic.rc_edges.clear();
    parasitic.rc_node_id_by_name.clear();
    return false;
  }

  if (net.driver_node_id != kInvalidFastStaNodeId && net.driver_node_id < context.nodes.size()) {
    const auto driver_key = makeRcNodeName(net.name, context.nodes.at(net.driver_node_id).location);
    if (const auto iter = parasitic.rc_node_id_by_name.find(driver_key); iter != parasitic.rc_node_id_by_name.end()) {
      parasitic.root_rc_node_id = iter->second;
    }
  }
  if (parasitic.root_rc_node_id == kInvalidFastStaRcNodeId) {
    parasitic.root_rc_node_id = 0U;
  }
  updateGroundCouplingTotals(parasitic);
  return true;
}

auto FastStaParasitics::buildNetParasiticFromRouteTree(FastStaClockContext& context, FastStaNetId net_id, const Net& net,
                                                       const ClockSteinerTree<int>& route_tree) -> bool
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

  std::vector<FastStaRcNodeId> rc_node_by_route_node(route_tree.node_count(), kInvalidFastStaRcNodeId);
  for (const auto& route_node : route_tree.get_nodes()) {
    if (route_node.id >= rc_node_by_route_node.size()) {
      return false;
    }

    auto terminal_node_id = kInvalidFastStaNodeId;
    if (route_node.is_terminal) {
      const auto terminal_iter = context.node_id_by_name.find(route_node.name);
      if (terminal_iter == context.node_id_by_name.end() || terminal_iter->second >= context.nodes.size()) {
        return false;
      }
      terminal_node_id = terminal_iter->second;
    }

    const auto rc_node_id = parasitic.rc_nodes.size();
    rc_node_by_route_node.at(route_node.id) = rc_node_id;
    parasitic.rc_node_id_by_name[route_node.name] = rc_node_id;
    parasitic.rc_nodes.push_back(FastStaRcNode{
        .name = route_node.name,
        .terminal_node_id = terminal_node_id,
    });
  }

  for (const auto& route_edge : route_tree.get_edges()) {
    if (route_edge.source_node_id >= rc_node_by_route_node.size() || route_edge.target_node_id >= rc_node_by_route_node.size()) {
      return false;
    }
    const auto from_id = rc_node_by_route_node.at(route_edge.source_node_id);
    const auto to_id = rc_node_by_route_node.at(route_edge.target_node_id);
    if (from_id == kInvalidFastStaRcNodeId || to_id == kInvalidFastStaRcNodeId || from_id == to_id) {
      return false;
    }

    const auto distance_dbu = routedEdgeDistanceDbu(route_edge);
    appendRcEdgeWithCapacitanceProfile(context, parasitic, from_id, to_id, distance_dbu);
  }

  const auto root_route_node_id = route_tree.get_root();
  if (root_route_node_id >= rc_node_by_route_node.size()) {
    return false;
  }
  parasitic.root_rc_node_id = rc_node_by_route_node.at(root_route_node_id);
  updateGroundCouplingTotals(parasitic);
  updateNetLoad(context, fast_net);
  return true;
}

auto FastStaParasitics::reduceToPiElmore(FastStaClockContext& context, FastStaNetId net_id) -> bool
{
  if (net_id >= context.nets.size()) {
    return false;
  }
  auto& net = context.nets.at(net_id);
  auto& parasitic = net.parasitic;
  if (parasitic.pre_reduced_pi_elmore) {
    auto total_cap_pf = 0.0;
    auto driver_total_cap_pf = 0.0;
    for (auto& rc_node : parasitic.rc_nodes) {
      if (rc_node.terminal_node_id != kInvalidFastStaNodeId && rc_node.terminal_node_id < context.nodes.size()) {
        rc_node.pin_cap_pf = std::max(0.0, context.nodes.at(rc_node.terminal_node_id).input_cap_pf);
      }
      rc_node.cap_pf = std::max(0.0, rc_node.wire_cap_pf + rc_node.pin_cap_pf);
      rc_node.driver_cap_pf = std::max(0.0, rc_node.driver_wire_cap_pf) + rc_node.pin_cap_pf;
      total_cap_pf += rc_node.cap_pf;
      driver_total_cap_pf += rc_node.driver_cap_pf;
    }
    parasitic.total_cap_pf = total_cap_pf;
    parasitic.driver_total_cap_pf = driver_total_cap_pf;
    parasitic.driver_pi = parasitic.pi;
    updateGroundCouplingTotals(parasitic);
    net.load_cap_pf = total_cap_pf;
    parasitic.valid = true;
    return true;
  }
  if (parasitic.rc_nodes.empty() || parasitic.root_rc_node_id >= parasitic.rc_nodes.size()) {
    parasitic.ground_cap_pf = 0.0;
    parasitic.coupling_cap_pf = 0.0;
    parasitic.timing_coupling_factor = 0.0;
    parasitic.total_cap_pf = net.load_cap_pf;
    parasitic.pi.near_cap_pf = net.load_cap_pf;
    parasitic.pi.resistance_ohm = 0.0;
    parasitic.pi.far_cap_pf = 0.0;
    parasitic.driver_total_cap_pf = net.load_cap_pf;
    parasitic.driver_pi = parasitic.pi;
    parasitic.valid = true;
    return true;
  }

  const auto adjacency = buildAdjacency(parasitic);

  struct MomentState
  {
    double y1 = 0.0;
    double y2 = 0.0;
    double y3 = 0.0;
    double downstream_cap = 0.0;
  };

  auto reduce_pi_model = [&](bool use_driver_cap) -> PiReductionResult {
    std::unordered_set<FastStaRcNodeId> visited;
    visited.reserve(parasitic.rc_nodes.size());
    std::function<MomentState(FastStaRcNodeId, FastStaRcNodeId)> reduce_moments = [&](FastStaRcNodeId node_id, FastStaRcNodeId parent_id) -> MomentState {
      visited.insert(node_id);
      auto& node = parasitic.rc_nodes.at(node_id);
      const auto node_cap_pf = std::max(0.0, use_driver_cap ? node.driver_cap_pf : node.cap_pf);
      MomentState state{
          .y1 = node_cap_pf,
          .y2 = 0.0,
          .y3 = 0.0,
          .downstream_cap = node_cap_pf,
      };

      for (const auto& [child_id, resistance_ohm] : adjacency.at(node_id)) {
        if (child_id == parent_id || visited.contains(child_id)) {
          continue;
        }
        auto child_state = reduce_moments(child_id, node_id);
        state.y1 += child_state.y1;
        state.y2 += child_state.y2 - resistance_ohm * child_state.y1 * child_state.y1;
        state.y3 += child_state.y3 - 2.0 * resistance_ohm * child_state.y1 * child_state.y2
                    + resistance_ohm * resistance_ohm * child_state.y1 * child_state.y1 * child_state.y1;
        state.downstream_cap += child_state.downstream_cap;
      }

      if (use_driver_cap) {
        node.driver_downstream_cap_pf = state.downstream_cap;
      } else {
        node.downstream_cap_pf = state.downstream_cap;
      }
      return state;
    };

    const auto root_state = reduce_moments(parasitic.root_rc_node_id, kInvalidFastStaRcNodeId);
    PiReductionResult result{.total_cap_pf = std::max(0.0, root_state.y1), .pi = {}};
    if (std::abs(root_state.y2) <= 1e-18 || std::abs(root_state.y3) <= 1e-18) {
      result.pi.near_cap_pf = result.total_cap_pf;
      result.pi.far_cap_pf = 0.0;
      result.pi.resistance_ohm = 0.0;
      return result;
    }

    const auto c1 = root_state.y2 * root_state.y2 / root_state.y3;
    const auto c2 = root_state.y1 - c1;
    const auto rpi = -root_state.y3 * root_state.y3 / (root_state.y2 * root_state.y2 * root_state.y2);
    result.pi.near_cap_pf = std::isfinite(c2) ? std::max(0.0, c2) : result.total_cap_pf;
    result.pi.far_cap_pf = std::isfinite(c1) ? std::max(0.0, c1) : 0.0;
    result.pi.resistance_ohm = std::isfinite(rpi) ? std::max(0.0, rpi) : 0.0;
    return result;
  };

  const auto physical_result = reduce_pi_model(false);
  parasitic.total_cap_pf = physical_result.total_cap_pf;
  parasitic.pi = physical_result.pi;

  const auto driver_result = reduce_pi_model(true);
  parasitic.driver_total_cap_pf = driver_result.total_cap_pf;
  parasitic.driver_pi = driver_result.pi;

  std::unordered_set<FastStaRcNodeId> visited;
  visited.reserve(parasitic.rc_nodes.size());
  std::function<void(FastStaRcNodeId, FastStaRcNodeId, double)> update_elmore
      = [&](FastStaRcNodeId node_id, FastStaRcNodeId parent_id, double elmore_delay_ns) -> void {
    visited.insert(node_id);
    auto& node = parasitic.rc_nodes.at(node_id);
    node.elmore_delay_ns = std::max(0.0, elmore_delay_ns);
    for (const auto& [child_id, resistance_ohm] : adjacency.at(node_id)) {
      if (child_id == parent_id || visited.contains(child_id)) {
        continue;
      }
      const auto child_elmore_ns = elmore_delay_ns + resistance_ohm * parasitic.rc_nodes.at(child_id).downstream_cap_pf * 1e-3;
      update_elmore(child_id, node_id, child_elmore_ns);
    }
  };
  update_elmore(parasitic.root_rc_node_id, kInvalidFastStaRcNodeId, 0.0);
  updateGroundCouplingTotals(parasitic);
  net.load_cap_pf = parasitic.total_cap_pf;
  parasitic.valid = true;
  return true;
}

}  // namespace icts
