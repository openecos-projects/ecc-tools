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
 * @file FastSTABuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Initialization bridge from committed CTS state to fast STA context.
 */

#include "FastSTABuilder.hh"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTAClockOverlay.hh"
#include "FastSTAClockState.hh"
#include "FastSTAGraphImport.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTAParasitics.hh"
#include "Logger.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"

namespace icts {

using fast_sta::AppendClock;
using fast_sta::AppendTimingGraph;
using fast_sta::CollectClocks;
using fast_sta::CollectRoutes;
using fast_sta::MergeLibertyCell;
using fast_sta::RequiresPropagationBufferModel;

class Pin;

namespace {

auto applyEnvironment(const FastStaEnvironment& environment, FastStaContext& context) -> void
{
  if (environment.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastStaBuilder: Wrapper is not bound.");
  }
  if (environment.dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "FastStaBuilder: DBU-per-micron is unavailable.");
  }
  if (environment.routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastStaBuilder: routing layer is not configured.");
  }
  context.wrapper = environment.wrapper;
  context.liberty_revision = environment.wrapper->queryLibertyRevision();
  context.dbu_per_um = environment.dbu_per_um;
  context.routing_layer = environment.routing_layer;
  context.wire_width_um = environment.wire_width_um;
  context.root_input_slew_ns = std::max(0.0, environment.root_input_slew_ns);
  context.worker_count = std::max<std::size_t>(1U, environment.worker_count);
}

auto collectPropagationBufferModelsAndNetLimits(const FastStaEnvironment& environment, const std::vector<const Clock*>& clocks, FastStaContext& context,
                                                std::string& failure_reason) -> bool
{
  auto& wrapper = *environment.wrapper;
  std::unordered_set<std::string> loaded_buffer_models;
  for (auto& node : context.nodes) {
    // Sink pin capacitance and slew are mandatory but are resolved independently by collectSinkPinCaps. Only propagation buffers drive DMP timing and power.
    if (!RequiresPropagationBufferModel(node) || node.cell_master.empty()) {
      continue;
    }
    if (!loaded_buffer_models.contains(node.cell_master)) {
      const auto model = FastStaLiberty::extractBufferCell(wrapper, node.cell_master);
      if (!model.has_value()) {
        failure_reason = "liberty_cell_unavailable:" + node.cell_master;
        return false;
      }
      MergeLibertyCell(*model, context);
      auto& cell = context.liberty_cell_by_master.at(node.cell_master);
      cell.timing_arc = model->timing_arc;
      cell.input_port = model->input_port;
      cell.output_port = model->output_port;
      cell.input_cap_pf = model->input_cap_pf;
      cell.input_cap_pf_by_timing = model->input_cap_pf_by_timing;
      cell.input_cap_profile_available = model->input_cap_profile_available;
      cell.output_cap_limit_pf = model->output_cap_limit_pf;
      cell.input_slew_limit_ns = model->input_slew_limit_ns;
      cell.output_slew_limit_ns = model->output_slew_limit_ns;
      loaded_buffer_models.insert(node.cell_master);
    }
    const auto& liberty_cell = context.liberty_cell_by_master.at(node.cell_master);
    if (node.kind == FastStaNodeKind::kBufferInput) {
      node.input_cap_pf = liberty_cell.input_cap_pf;
      node.input_cap_pf_by_timing = liberty_cell.input_cap_pf_by_timing;
      node.input_cap_profile_available = liberty_cell.input_cap_profile_available;
      if (node.slew_limit_from_master || node.max_slew_ns <= 0.0) {
        node.max_slew_ns = liberty_cell.input_slew_limit_ns;
        node.slew_limit_from_master = true;
      }
      node.port_name = liberty_cell.input_port;
      node.input = true;
    } else {
      if (node.slew_limit_from_master || node.max_slew_ns <= 0.0) {
        node.max_slew_ns = liberty_cell.output_slew_limit_ns;
        node.slew_limit_from_master = true;
      }
      node.port_name = liberty_cell.output_port;
      node.output = true;
      if (liberty_cell.timing_arc.positive_unate != liberty_cell.timing_arc.negative_unate) {
        node.logic_function = (liberty_cell.timing_arc.negative_unate ? "!" : "") + liberty_cell.input_port;
      }
    }
  }
  for (auto& net : context.nets) {
    if (environment.max_cap_pf.has_value() && *environment.max_cap_pf > 0.0) {
      net.max_cap_pf = *environment.max_cap_pf;
      net.cap_limit_from_master = false;
      continue;
    }
    const auto source_clock = std::ranges::find_if(clocks, [&](const Clock* clock) -> bool {
      const auto* source_net = clock->get_clock_source_net();
      return source_net != nullptr && net.name == source_net->get_name();
    });
    if (source_clock != clocks.end()) {
      const auto source_cap_limit_pf
          = wrapper.queryClockSourceDriveCapLimit({.clock_source = (*source_clock)->get_clock_source(), .configured_max_cap_pf = environment.max_cap_pf});
      net.max_cap_pf = source_cap_limit_pf.value_or(0.0);
      continue;
    }
    if (net.driver_node_id == kInvalidFastStaNodeId) {
      continue;
    }
    const auto& driver = context.nodes.at(net.driver_node_id);
    if (const auto iter = context.liberty_cell_by_master.find(driver.cell_master); iter != context.liberty_cell_by_master.end()) {
      net.max_cap_pf = iter->second.output_cap_limit_pf;
      net.cap_limit_from_master = true;
    }
  }
  FastStaParasitics::updateNetLoads(context);
  return true;
}

auto collectSinkPinCaps(const FastStaEnvironment& environment, const Clock& clock, FastStaContext& context, std::string& failure_reason) -> bool
{
  auto& wrapper = *environment.wrapper;
  for (auto* pin : clock.get_loads()) {
    if (pin == nullptr) {
      continue;
    }
    const auto node_iter = context.node_id_by_name.find(Design::getPinFullName(pin));
    if (node_iter == context.node_id_by_name.end() || node_iter->second >= context.nodes.size()) {
      continue;
    }
    auto& node = context.nodes.at(node_iter->second);
    const auto input_cap_pf = wrapper.queryPinCapacitance(pin);
    const auto max_slew_ns = wrapper.queryPinSlewLimit({.pin = pin, .configured_max_sink_tran_ns = environment.max_sink_tran_ns});
    if (!input_cap_pf.has_value()) {
      failure_reason = "sink_pin_capacitance_unavailable:";
      failure_reason += Design::getPinFullName(pin);
      return false;
    }
    node.input_cap_pf = *input_cap_pf;
    for (const auto& [analysis_index, early] : {std::pair{0U, true}, std::pair{1U, false}}) {
      for (const auto& [transition_index, transition] : {std::pair{0U, WrapperTimingTransition::kRise}, std::pair{1U, WrapperTimingTransition::kFall}}) {
        const auto cap = wrapper.queryPinCapacitance(pin, early, transition);
        if (!cap.has_value()) {
          failure_reason = "sink_pin_capacitance_profile_unavailable:" + Design::getPinFullName(pin);
          return false;
        }
        node.input_cap_pf_by_timing.at(analysis_index).at(transition_index) = *cap;
      }
    }
    node.input_cap_profile_available = true;
    node.max_slew_ns = max_slew_ns.value_or(0.0);
  }
  return true;
}

auto applyClockRoutes(const FastStaClockRouteGeometry& geometry, FastStaContext& context, std::string& failure_reason) -> bool
{
  if (geometry.clock_nets.empty()) {
    return true;
  }
  if (geometry.design_dbu_per_um != context.dbu_per_um) {
    failure_reason = "route_geometry_dbu_mismatch";
    return false;
  }
  for (const auto& route : geometry.clock_nets) {
    const auto net = context.net_id_by_name.find(route.net_name);
    if (net == context.net_id_by_name.end()) {
      failure_reason = "clock_route_net_unavailable:" + route.net_name;
      return false;
    }
    if (!route.routed_segments.empty() && !FastStaParasitics::buildNetParasiticFromSegments(context, net->second, route.routed_segments)) {
      failure_reason = "clock_route_rc_tree_invalid:" + route.net_name;
      return false;
    }
  }
  return true;
}

}  // namespace

auto FastStaBuilder::buildContext(const FastStaEnvironment& environment, const FastStaBuildInput& input, const FastStaContext* prepared_context) -> BuildResult
{
  if (environment.wrapper == nullptr) {
    return BuildResult{.failure_reason = "fast_sta_environment_wrapper_unavailable"};
  }
  if (environment.dbu_per_um <= 0) {
    return BuildResult{.failure_reason = "fast_sta_environment_dbu_unavailable"};
  }
  if (environment.routing_layer <= 0) {
    return BuildResult{.failure_reason = "fast_sta_environment_routing_layer_unavailable"};
  }
  if (input.route_geometry != nullptr && input.route_geometry->design_dbu_per_um != environment.dbu_per_um) {
    return BuildResult{.failure_reason = "route_geometry_dbu_mismatch"};
  }
  FastStaContext context;
  std::string failure_reason;
  const auto clocks = CollectClocks(input);
  for (const auto* clock : clocks) {
    if (clock == nullptr) {
      return BuildResult{.failure_reason = "clock_context_is_null"};
    }
    if (!AppendClock(*clock, clock == input.clock, context, failure_reason)) {
      return BuildResult{.failure_reason = std::move(failure_reason)};
    }
  }
  if (input.clock != nullptr) {
    context.clock_name = input.clock->get_clock_name();
    context.clock_net_name = input.clock->get_clock_net_name();
    context.clock_period_ns = input.clock->get_clock_period_ns();
    const auto source = context.node_id_by_name.find(Design::getPinFullName(input.clock->get_clock_source()));
    if (source != context.node_id_by_name.end()) {
      context.source_node_id = source->second;
    }
  }

  applyEnvironment(environment, context);
  context.propagate_all_clocks = input.propagate_all_clocks;
  if (input.constraints != nullptr) {
    context.constraints = *input.constraints;
    context.input_constraints = *input.constraints;
  }

  for (FastStaNodeId id = 0U; id < context.nodes.size(); ++id) {
    context.clock_overlay_node_ids.push_back(id);
  }
  for (FastStaNetId id = 0U; id < context.nets.size(); ++id) {
    context.clock_overlay_net_ids.push_back(id);
  }
  context.clock_routes = CollectRoutes(input);
  for (const auto& geometry : context.clock_routes) {
    if (!applyClockRoutes(geometry, context, failure_reason)) {
      return BuildResult{.failure_reason = std::move(failure_reason)};
    }
  }

  if (input.timing_graph != nullptr && !AppendTimingGraph(environment, *input.timing_graph, context, failure_reason)) {
    return BuildResult{.failure_reason = std::move(failure_reason)};
  }

  if (prepared_context != nullptr) {
    if (prepared_context->wrapper != context.wrapper || prepared_context->liberty_revision != context.liberty_revision
        || prepared_context->dbu_per_um != context.dbu_per_um) {
      return {.failure_reason = "clock_topology_input_generation_mismatch"};
    }
    for (auto& net : context.nets) {
      if (!net.parasitic.rc_nodes.empty()) {
        continue;
      }
      const auto found = prepared_context->net_id_by_name.find(net.name);
      if (found == prepared_context->net_id_by_name.end() || found->second >= prepared_context->input_net_count) {
        continue;
      }
      // Unchanged traced-input nets retain the input graph's RC authority.
      // Removed synthesized route geometry must not reuse an older route.
      const bool had_routed_geometry = std::ranges::any_of(prepared_context->clock_routes, [&](const auto& route) -> bool {
        return std::ranges::any_of(route.clock_nets,
                                   [&](const auto& route_net) -> bool { return route_net.net_name == net.name && !route_net.routed_segments.empty(); });
      });
      const auto& previous = prepared_context->nets.at(found->second);
      if (had_routed_geometry || previous.load_node_ids.size() != net.load_node_ids.size()) {
        continue;
      }
      const auto same_pin = [&](FastStaNodeId current_id, FastStaNodeId previous_id) -> bool {
        const auto& current = context.nodes.at(current_id);
        const auto& original = prepared_context->nodes.at(previous_id);
        return current.name == original.name && current.location.x_dbu == original.location.x_dbu && current.location.y_dbu == original.location.y_dbu;
      };
      bool matching = same_pin(net.driver_node_id, previous.driver_node_id);
      for (std::size_t index = 0U; matching && index < net.load_node_ids.size(); ++index) {
        matching = same_pin(net.load_node_ids.at(index), previous.load_node_ids.at(index));
      }
      if (!matching) {
        continue;
      }
      net.parasitic = previous.parasitic;
      const auto remap = [&](FastStaNodeId id) -> FastStaNodeId {
        return id < prepared_context->nodes.size() ? context.node_id_by_name.at(prepared_context->nodes.at(id).name) : kInvalidFastStaNodeId;
      };
      for (auto& rc : net.parasitic.rc_nodes) {
        rc.terminal_node_id = remap(rc.terminal_node_id);
        for (auto& id : rc.terminal_node_ids) {
          id = remap(id);
        }
      }
    }
  }

  if (input.committed_design != nullptr) {
    for (std::size_t net_id = 0U; net_id < context.nets.size(); ++net_id) {
      const auto& net = context.nets.at(net_id);
      if (net.domain == FastStaNetDomain::kClock && net.parasitic.rc_nodes.empty() && !FastStaParasitics::buildNetParasiticFromSegments(context, net_id, {})) {
        return BuildResult{.failure_reason = "clock_route_rc_unavailable:" + net.name};
      }
    }
  }
  for (const auto* clock : clocks) {
    if (!collectSinkPinCaps(environment, *clock, context, failure_reason)) {
      return BuildResult{.failure_reason = std::move(failure_reason)};
    }
  }
  if (!clocks.empty() && !collectPropagationBufferModelsAndNetLimits(environment, clocks, context, failure_reason)) {
    return BuildResult{.failure_reason = std::move(failure_reason)};
  }
  context.input_node_count = context.nodes.size();
  context.input_net_count = context.nets.size();
  return BuildResult{.context = std::move(context), .failure_reason = {}};
}

auto FastStaBuilder::validateTimingGraphJoins(const WrapperTimingGraph& graph, const FastStaContext& context) -> std::optional<std::string>
{
  std::unordered_set<std::string> logic_pin_names;
  logic_pin_names.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    logic_pin_names.insert(node.pin_name);
  }
  for (const auto& launch : graph.launches) {
    if (!logic_pin_names.contains(launch.output_pin)) {
      return "timing_graph_launch_output_node_unindexed:" + launch.output_pin;
    }
    if (!context.node_id_by_name.contains(launch.clock_pin)) {
      return "timing_graph_launch_clock_node_unindexed:" + launch.clock_pin;
    }
  }
  for (const auto& check : graph.checks) {
    if (!logic_pin_names.contains(check.data_pin)) {
      return "timing_graph_check_data_node_unindexed:" + check.data_pin;
    }
    if (!context.node_id_by_name.contains(check.clock_pin)) {
      return "timing_graph_check_clock_node_unindexed:" + check.clock_pin;
    }
  }
  return std::nullopt;
}

auto FastStaBuilder::injectNetRouteTree(FastStaContext& context, const Net& net, const ClockSteinerTree<int>& route_tree) -> bool
{
  const auto net_iter = context.net_id_by_name.find(net.get_name());
  if (net_iter == context.net_id_by_name.end() || net_iter->second >= context.nets.size()) {
    return false;
  }
  return FastStaParasitics::buildNetParasiticFromRouteTree(context, net_iter->second, net, route_tree);
}

}  // namespace icts
