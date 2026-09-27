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
 * @file FastSTAGraphImport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Join physical nodes, nets, timing arcs, launches and checks.
 */

#include "FastSTAGraphImport.hh"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTABuilder.hh"
#include "FastSTAClockState.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTAParasitics.hh"
#include "io/Wrapper.hh"

namespace icts::fast_sta {
namespace {

auto toFastStaTransition(WrapperTimingTransition transition) -> FastStaTransition
{
  return transition == WrapperTimingTransition::kRise ? FastStaTransition::kRise : FastStaTransition::kFall;
}

auto matchesSnapshotNet(const WrapperTimingNet& snapshot, const FastStaNet& net, const FastStaContext& context,
                        const std::unordered_map<std::string, const WrapperTimingNode*>& snapshot_nodes) -> bool
{
  if (net.driver_node_id >= context.nodes.size() || context.nodes.at(net.driver_node_id).name != snapshot.driver_pin
      || net.load_node_ids.size() != snapshot.load_pins.size()) {
    return false;
  }
  const std::unordered_set<std::string> load_names(snapshot.load_pins.begin(), snapshot.load_pins.end());
  const auto same_location = [&](FastStaNodeId id) -> bool {
    const auto& node = context.nodes.at(id);
    const auto source = snapshot_nodes.find(node.name);
    return source != snapshot_nodes.end() && source->second->x_dbu == node.location.x_dbu && source->second->y_dbu == node.location.y_dbu;
  };
  return same_location(net.driver_node_id) && load_names.size() == net.load_node_ids.size()
         && std::ranges::all_of(net.load_node_ids, [&](auto id) -> bool { return load_names.contains(context.nodes.at(id).name) && same_location(id); });
}

auto applySnapshotRc(const WrapperTimingNet& source, FastStaNetId net_id, FastStaContext& context, std::string& failure_reason) -> bool
{
  std::vector<FastStaRcSegment> segments;
  segments.reserve(source.rc_segments.size());
  for (const auto& segment : source.rc_segments) {
    segments.push_back(FastStaRcSegment{
        .begin = {.x_dbu = segment.begin_x_dbu, .y_dbu = segment.begin_y_dbu},
        .end = {.x_dbu = segment.end_x_dbu, .y_dbu = segment.end_y_dbu},
        .resistance_ohm = segment.resistance_ohm,
        .capacitance_pf = segment.capacitance_pf,
        .electrical_values_valid = true,
    });
  }
  if (!FastStaParasitics::buildNetParasiticFromSegments(context, net_id, segments)) {
    failure_reason = (source.physically_zero_length ? "timing_graph_zero_length_rc_failed:" : "timing_graph_rc_tree_failed:") + source.net_name;
    return false;
  }
  auto& net = context.nets.at(net_id);
  net.wire_resistance_ohm = source.wire_resistance_ohm;
  net.wire_cap_pf = source.wire_cap_pf;
  net.total_wirelength_dbu = source.total_wirelength_dbu;
  return true;
}

}  // namespace

auto MergeLibertyCell(FastStaLibertyCell model, FastStaContext& context) -> void
{
  const auto master = model.cell_master;
  auto [iter, inserted] = context.liberty_cell_by_master.try_emplace(master, std::move(model));
  if (inserted) {
    return;
  }
  auto& target = iter->second;
  for (auto& arc : model.timing_arcs) {
    const auto exists = std::ranges::find_if(target.timing_arcs, [&](const auto& current) -> bool {
      return current.from_port == arc.from_port && current.to_port == arc.to_port && current.edge_triggered == arc.edge_triggered
             && current.trigger_transition == arc.trigger_transition && current.variant_index == arc.variant_index
             && current.positive_unate == arc.positive_unate && current.negative_unate == arc.negative_unate && current.conditional == arc.conditional
             && current.library_name == arc.library_name;
    });
    if (exists == target.timing_arcs.end()) {
      target.timing_arcs.push_back(std::move(arc));
    }
  }
}

auto AppendTimingGraph(const FastStaEnvironment& environment, const WrapperTimingGraph& graph, FastStaContext& context, std::string& failure_reason) -> bool
{
  if (!graph.complete()) {
    failure_reason = graph.diagnostic.empty() ? "timing_graph_unavailable" : graph.diagnostic;
    return false;
  }
  auto& wrapper = *environment.wrapper;
  const auto additional_nodes = static_cast<std::size_t>(
      std::ranges::count_if(graph.nodes, [&](const auto& node) -> bool { return !context.node_id_by_name.contains(node.pin_name); }));
  context.nodes.reserve(context.nodes.size() + additional_nodes);
  context.node_id_by_name.reserve(context.nodes.size() + additional_nodes);
  context.nets.reserve(context.nets.size() + graph.nets.size());
  context.timing_arcs.reserve(graph.arcs.size());
  context.timing_launches.reserve(graph.launches.size());
  context.timing_checks.reserve(graph.checks.size());
  std::unordered_map<std::string, FastStaNodeId> node_ids;
  node_ids.reserve(graph.nodes.size());
  std::unordered_map<std::string, const WrapperTimingNode*> snapshot_nodes;
  snapshot_nodes.reserve(graph.nodes.size());
  for (const auto& source_node : graph.nodes) {
    if (source_node.pin_name.empty()) {
      failure_reason = "timing_graph_node_name_empty";
      return false;
    }
    snapshot_nodes.emplace(source_node.pin_name, &source_node);
    if (const auto existing = context.node_id_by_name.find(source_node.pin_name); existing != context.node_id_by_name.end()) {
      auto& node = context.nodes.at(existing->second);
      // The complete graph owns non-clock pin facts. Physical buffer pairs are
      // retained, while clock reachability is re-established by the SDC/DAG
      // resolver instead of making every pin on a clock inst a clock signal.
      node.domain = FastStaNodeDomain::kLogic;
      if (node.cell_master == source_node.cell_master) {
        node.input_cap_pf = source_node.input_cap_pf;
        node.input_cap_pf_by_timing = source_node.input_cap_pf_by_timing;
        node.input_cap_profile_available = source_node.input_cap_profile_available;
        node.max_slew_ns = source_node.max_slew_ns;
        node.slew_limit_from_master = source_node.slew_limit_from_master;
      }
      node.port_name = source_node.port_name;
      node.logic_function = source_node.logic_function;
      node.input = source_node.input;
      node.output = source_node.output;
      node.clock_pin = source_node.clock_pin;
      node.top_level = source_node.top_level;
      node_ids.emplace(source_node.pin_name, existing->second);
      continue;
    }
    const auto node_id = context.nodes.size();
    context.node_id_by_name.emplace(source_node.pin_name, node_id);
    context.nodes.push_back(FastStaNode{.kind = FastStaNodeKind::kSink,
                                        .name = source_node.pin_name,
                                        .inst_name = source_node.inst_name,
                                        .pin_name = source_node.pin_name,
                                        .cell_master = source_node.cell_master,
                                        .location = FastStaPoint{.x_dbu = source_node.x_dbu, .y_dbu = source_node.y_dbu},
                                        .input_cap_pf = std::max(0.0, source_node.input_cap_pf),
                                        .input_cap_pf_by_timing = source_node.input_cap_pf_by_timing,
                                        .max_slew_ns = std::max(0.0, source_node.max_slew_ns),
                                        .incoming_net_id = kInvalidFastStaNetId,
                                        .output_net_ids = {},
                                        .timing = {},
                                        .internal_power_w = 0.0,
                                        .leakage_power_w = 0.0,
                                        .area_um2 = 0.0,
                                        .early_timing = {},
                                        .late_timing = {},
                                        .arrival_seed_early_ns = 0.0,
                                        .arrival_seed_late_ns = 0.0,
                                        .slew_seed_early_ns = 0.0,
                                        .slew_seed_late_ns = 0.0,
                                        .clock_arrival_early_ns = 0.0,
                                        .clock_arrival_late_ns = 0.0,
                                        .top_level = source_node.top_level,
                                        .domain = FastStaNodeDomain::kLogic,
                                        .port_name = source_node.port_name,
                                        .logic_function = source_node.logic_function,
                                        .input = source_node.input,
                                        .output = source_node.output,
                                        .clock_pin = source_node.clock_pin});
    context.nodes.back().input_cap_profile_available = source_node.input_cap_profile_available;
    context.nodes.back().slew_limit_from_master = source_node.slew_limit_from_master;
    context.node_id_by_location.emplace(std::pair(source_node.x_dbu, source_node.y_dbu), node_id);
    node_ids.emplace(source_node.pin_name, node_id);
  }
  if (const auto join_failure = FastStaBuilder::validateTimingGraphJoins(graph, context); join_failure.has_value()) {
    failure_reason = join_failure.value();
    return false;
  }

  for (const auto& source_net : graph.nets) {
    if (source_net.net_name.empty() || source_net.driver_pin.empty() || source_net.load_pins.empty()) {
      failure_reason = "timing_graph_net_is_incomplete:" + source_net.net_name;
      return false;
    }
    if (const auto existing = context.net_id_by_name.find(source_net.net_name); existing != context.net_id_by_name.end()) {
      const auto& net = context.nets.at(existing->second);
      // A routed candidate owns its RC. An unchanged, unrouted clock net may
      // reuse the physical input RC only with identical terminals and positions.
      if (net.parasitic.rc_nodes.empty() && matchesSnapshotNet(source_net, net, context, snapshot_nodes)
          && !applySnapshotRc(source_net, existing->second, context, failure_reason)) {
        return false;
      }
      continue;
    }
    const auto driver_iter = node_ids.find(source_net.driver_pin);
    if (driver_iter == node_ids.end()) {
      failure_reason = "timing_graph_driver_pin_unindexed:" + source_net.driver_pin;
      return false;
    }
    const auto superseded = std::ranges::any_of(context.nodes.at(driver_iter->second).output_net_ids,
                                                [&](auto id) -> bool { return context.nets.at(id).domain == FastStaNetDomain::kClock; })
                            || std::ranges::any_of(source_net.load_pins, [&](const auto& pin) -> bool {
                                 const auto node = node_ids.find(pin);
                                 if (node == node_ids.end()) {
                                   return false;
                                 }
                                 const auto incoming = context.nodes.at(node->second).incoming_net_id;
                                 return incoming < context.nets.size() && context.nets.at(incoming).domain == FastStaNetDomain::kClock;
                               });
    if (superseded) {
      continue;
    }
    FastStaNet net;
    net.name = source_net.net_name;
    net.driver_node_id = driver_iter->second;
    net.domain = FastStaNetDomain::kLogic;
    net.wire_resistance_ohm = source_net.wire_resistance_ohm;
    net.wire_cap_pf = source_net.wire_cap_pf;
    net.total_wirelength_dbu = source_net.total_wirelength_dbu;
    net.wire_delay_precomputed = false;
    for (const auto& load_pin : source_net.load_pins) {
      const auto load_iter = node_ids.find(load_pin);
      if (load_iter == node_ids.end() || load_iter->second == net.driver_node_id) {
        failure_reason = "timing_graph_load_pin_unindexed:" + load_pin;
        return false;
      }
      net.load_node_ids.push_back(load_iter->second);
      context.nodes.at(load_iter->second).incoming_net_id = context.nets.size();
    }
    net.load_node_ids.erase(std::ranges::unique(net.load_node_ids).begin(), net.load_node_ids.end());
    const auto net_id = context.nets.size();
    context.net_id_by_name.emplace(net.name, net_id);
    context.nodes.at(net.driver_node_id).output_net_ids.push_back(net_id);
    context.nets.push_back(std::move(net));
    if (!applySnapshotRc(source_net, net_id, context, failure_reason)) {
      return false;
    }
  }

  for (const auto& source_arc : graph.arcs) {
    const auto from_iter = node_ids.find(source_arc.input_pin);
    const auto to_iter = node_ids.find(source_arc.output_pin);
    if (from_iter == node_ids.end() || to_iter == node_ids.end() || source_arc.cell_master.empty() || source_arc.input_port.empty()
        || source_arc.output_port.empty()) {
      failure_reason = "timing_graph_arc_is_incomplete:" + source_arc.inst_name;
      return false;
    }
    // Immutable signal connectivity does not make an original buffer master
    // immutable: accepted CTS sizing is owned by the current Design overlay.
    const auto& cell_master = context.nodes.at(from_iter->second).cell_master;
    if (cell_master.empty() || context.nodes.at(to_iter->second).cell_master != cell_master) {
      failure_reason = "timing_graph_arc_master_join_invalid:" + source_arc.inst_name;
      return false;
    }
    auto liberty_iter = context.liberty_cell_by_master.find(cell_master);
    if (liberty_iter == context.liberty_cell_by_master.end()) {
      const auto model = FastStaLiberty::extractCellArc(wrapper, cell_master, source_arc.input_port, source_arc.output_port);
      if (!model.has_value()) {
        failure_reason = "timing_graph_liberty_arc_unavailable:" + cell_master + ":" + source_arc.input_port + ":" + source_arc.output_port;
        return false;
      }
      MergeLibertyCell(*model, context);
      liberty_iter = context.liberty_cell_by_master.find(cell_master);
    } else {
      const auto exists = std::ranges::find_if(liberty_iter->second.timing_arcs, [&](const auto& timing_arc) -> bool {
        return timing_arc.from_port == source_arc.input_port && timing_arc.to_port == source_arc.output_port;
      });
      if (exists == liberty_iter->second.timing_arcs.end()) {
        const auto model = FastStaLiberty::extractCellArc(wrapper, cell_master, source_arc.input_port, source_arc.output_port);
        if (!model.has_value()) {
          failure_reason = "timing_graph_liberty_arc_unavailable:" + cell_master + ":" + source_arc.input_port + ":" + source_arc.output_port;
          return false;
        }
        MergeLibertyCell(*model, context);
      }
    }
    context.timing_arcs.push_back(FastStaTimingArc{.from_node_id = from_iter->second,
                                                   .to_node_id = to_iter->second,
                                                   .cell_master = cell_master,
                                                   .from_port = source_arc.input_port,
                                                   .to_port = source_arc.output_port,
                                                   .positive_unate = source_arc.positive_unate,
                                                   .negative_unate = source_arc.negative_unate,
                                                   .clock_gate_boundary = source_arc.clock_gate_boundary});
  }
  for (const auto& source_launch : graph.launches) {
    const auto output_iter = node_ids.find(source_launch.output_pin);
    const auto clock_iter = context.node_id_by_name.find(source_launch.clock_pin);
    if (output_iter == node_ids.end() || clock_iter == context.node_id_by_name.end()) {
      failure_reason = "timing_graph_launch_node_join_invalid:" + source_launch.output_pin + ":" + source_launch.clock_pin;
      return false;
    }
    const auto model = FastStaLiberty::extractLaunchArc(wrapper, source_launch.cell_master, source_launch.clock_port, source_launch.output_port,
                                                        toFastStaTransition(source_launch.clock_transition));
    if (!model.has_value()) {
      failure_reason = "timing_graph_launch_arc_unavailable:" + source_launch.cell_master + ":" + source_launch.clock_port + ":" + source_launch.output_port;
      return false;
    }
    MergeLibertyCell(*model, context);
    context.timing_launches.push_back(FastStaTimingLaunch{
        .output_node_id = output_iter->second,
        .clock_node_id = clock_iter->second,
        .cell_master = source_launch.cell_master,
        .clock_port = source_launch.clock_port,
        .output_port = source_launch.output_port,
        .clock_transition = toFastStaTransition(source_launch.clock_transition),
    });
  }
  for (const auto& source_check : graph.checks) {
    const auto data_iter = node_ids.find(source_check.data_pin);
    const auto clock_iter = context.node_id_by_name.find(source_check.clock_pin);
    if (data_iter == node_ids.end() || clock_iter == context.node_id_by_name.end()) {
      failure_reason = "timing_graph_check_node_join_invalid:" + source_check.data_pin + ":" + source_check.clock_pin;
      return false;
    }
    context.timing_checks.push_back(FastStaTimingCheck{
        .data_node_id = data_iter->second,
        .clock_node_id = clock_iter->second,
        .cell_master = source_check.cell_master,
        .clock_port = source_check.clock_port,
        .data_port = source_check.data_port,
        .kind = source_check.kind == WrapperTimingCheckKind::kSetup ? FastStaTimingCheckKind::kSetup : FastStaTimingCheckKind::kHold,
        .clock_transition = toFastStaTransition(source_check.clock_transition),
        .clock_gating = source_check.clock_gating,
    });
  }
  for (auto& net : context.nets) {
    if (net.domain != FastStaNetDomain::kLogic || net.driver_node_id >= context.nodes.size()) {
      continue;
    }
    const auto& driver = context.nodes.at(net.driver_node_id);
    const auto liberty_iter = context.liberty_cell_by_master.find(driver.cell_master);
    if (liberty_iter != context.liberty_cell_by_master.end()) {
      net.max_cap_pf = std::max(0.0, liberty_iter->second.output_cap_limit_pf);
      net.cap_limit_from_master = true;
    }
  }
  return true;
}

}  // namespace icts::fast_sta
