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
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTAClockState.hh"
#include "FastSTAClockTree.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTAParasitics.hh"
#include "Logger.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Net.hh"
#include "io/Wrapper.hh"

namespace icts {

class Pin;

namespace {

auto applyEnvironment(const FastStaEnvironment& environment, FastStaClockContext& context) -> void
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
  context.dbu_per_um = environment.dbu_per_um;
  context.routing_layer = environment.routing_layer;
  context.wire_width_um = environment.wire_width_um;
  context.root_input_slew_ns = std::max(0.0, environment.root_input_slew_ns);
}

auto queryWrapperBackedSinkPinCap(Wrapper& wrapper, const Pin* pin) -> std::optional<double>
{
  return wrapper.queryPinCapacitance(pin);
}

auto queryWrapperBackedSinkSlewLimit(const FastStaEnvironment& environment, const Pin* pin) -> std::optional<double>
{
  return environment.wrapper->queryPinSlewLimit(Wrapper::PinSlewLimitInput{
      .pin = pin,
      .configured_max_sink_tran_ns = environment.max_sink_tran_ns,
  });
}

auto queryWrapperBackedSourceCapLimit(const FastStaEnvironment& environment, const Clock& clock) -> std::optional<double>
{
  return environment.wrapper->queryClockSourceDriveCapLimit(Wrapper::ClockSourceDriveCapLimitInput{
      .clock_source = clock.get_clock_source(),
      .configured_max_cap_pf = environment.max_cap_pf,
  });
}

auto isSourceBoundaryNet(const Clock& clock, const FastStaClockContext& context, const FastStaNet& net) -> bool
{
  if (net.driver_node_id == context.source_node_id) {
    return true;
  }
  const auto* source_net = clock.get_clock_source_net();
  return source_net != nullptr && net.name == source_net->get_name();
}

auto requiresPropagationBufferModel(const FastStaNode& node) -> bool
{
  return node.kind == FastStaNodeKind::kBufferInput || node.kind == FastStaNodeKind::kBufferOutput;
}

auto collectPropagationBufferModelsAndNetLimits(const FastStaEnvironment& environment, const Clock& clock, FastStaClockContext& context,
                                                std::string& failure_reason) -> bool
{
  auto& wrapper = *environment.wrapper;
  for (auto& node : context.nodes) {
    // Sink pin capacitance and slew are mandatory but are resolved independently by collectSinkPinCaps. Only propagation buffers drive DMP timing and power.
    if (!requiresPropagationBufferModel(node) || node.cell_master.empty()) {
      continue;
    }
    if (!context.liberty_cell_by_master.contains(node.cell_master)) {
      const auto liberty_cell = FastStaLiberty::extractBufferCell(wrapper, node.cell_master);
      if (!liberty_cell.has_value()) {
        failure_reason = "liberty_cell_unavailable:" + node.cell_master;
        return false;
      }
      context.liberty_cell_by_master.emplace(node.cell_master, *liberty_cell);
    }
    const auto& liberty_cell = context.liberty_cell_by_master.at(node.cell_master);
    if (node.kind == FastStaNodeKind::kBufferInput) {
      node.input_cap_pf = liberty_cell.input_cap_pf;
      node.max_slew_ns = liberty_cell.input_slew_limit_ns;
    }
  }
  for (auto& net : context.nets) {
    if (environment.max_cap_pf.has_value() && *environment.max_cap_pf > 0.0) {
      net.max_cap_pf = *environment.max_cap_pf;
      continue;
    }
    if (isSourceBoundaryNet(clock, context, net)) {
      const auto source_cap_limit_pf = queryWrapperBackedSourceCapLimit(environment, clock);
      if (!source_cap_limit_pf.has_value()) {
        failure_reason = "source_drive_capacitance_unavailable:" + clock.get_clock_name();
        return false;
      }
      net.max_cap_pf = *source_cap_limit_pf;
      continue;
    }
    if (net.driver_node_id == kInvalidFastStaNodeId) {
      continue;
    }
    const auto& driver = context.nodes.at(net.driver_node_id);
    if (const auto iter = context.liberty_cell_by_master.find(driver.cell_master); iter != context.liberty_cell_by_master.end()) {
      net.max_cap_pf = iter->second.output_cap_limit_pf;
    }
  }
  FastStaParasitics::updateNetLoads(context);
  return true;
}

auto collectSinkPinCaps(const FastStaEnvironment& environment, const Clock& clock, FastStaClockContext& context, std::string& failure_reason) -> bool
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
    const auto input_cap_pf = queryWrapperBackedSinkPinCap(wrapper, pin);
    const auto max_slew_ns = queryWrapperBackedSinkSlewLimit(environment, pin);
    if (!input_cap_pf.has_value() || !max_slew_ns.has_value()) {
      failure_reason = !input_cap_pf.has_value() ? "sink_pin_capacitance_unavailable:" : "sink_slew_limit_unavailable:";
      failure_reason += Design::getPinFullName(pin);
      return false;
    }
    node.input_cap_pf = *input_cap_pf;
    node.max_slew_ns = *max_slew_ns;
  }
  return true;
}

}  // namespace

auto FastStaBuilder::buildClockContext(const FastStaEnvironment& environment, const FastStaClockBuildInput& input) -> BuildResult
{
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "FastStaBuilder: clock build input is null.");
  }
  const auto& clock = *input.clock;
  auto context
      = input.route_geometry == nullptr ? FastStaClockTree::buildFromClock(clock) : FastStaClockTree::buildFromClockRouteGeometry(clock, *input.route_geometry);

  applyEnvironment(environment, context);

  if (input.route_geometry != nullptr) {
    FastStaClockTree::applyRouteGeometry(context, *input.route_geometry);
  }

  std::string failure_reason;
  if (!collectSinkPinCaps(environment, clock, context, failure_reason)
      || !collectPropagationBufferModelsAndNetLimits(environment, clock, context, failure_reason)) {
    return BuildResult{.failure_reason = std::move(failure_reason)};
  }
  return BuildResult{.context = std::move(context), .failure_reason = {}};
}

auto FastStaBuilder::injectNetRouteTree(FastStaClockContext& context, const Net& net, const ClockSteinerTree<int>& route_tree) -> bool
{
  const auto net_iter = context.net_id_by_name.find(net.get_name());
  if (net_iter == context.net_id_by_name.end() || net_iter->second >= context.nets.size()) {
    return false;
  }
  return FastStaParasitics::buildNetParasiticFromRouteTree(context, net_iter->second, net, route_tree);
}

}  // namespace icts
