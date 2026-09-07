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
 * @file FastSTAChar.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Characterization sample context construction implementation for CTS fast STA.
 */

#include "FastSTAChar.hh"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastSTAClockState.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTAParasitics.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "Logger.hh"
#include "clock_net_parasitic/FastSTAClockNetParasitic.hh"
#include "io/Wrapper.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts {
namespace {

auto resolveDbuPerUm(const FastStaCharTopologySpec& spec) -> int
{
  if (spec.dbu_per_um.has_value()) {
    if (*spec.dbu_per_um <= 0) {
      CTSLOG.error(Loc::current(), "FastStaChar: explicit DBU-per-micron must be positive.");
    }
    return *spec.dbu_per_um;
  }

  CTSLOG.error(Loc::current(), "FastStaChar: DBU-per-micron must be explicitly provided in the characterization topology spec.");
  return 0;
}

auto makePoint(double x_um, int dbu_per_um) -> FastStaPoint
{
  return FastStaPoint{.x_dbu = static_cast<int>(x_um * static_cast<double>(dbu_per_um)), .y_dbu = 0};
}

auto appendNode(FastStaClockContext& context, FastStaNode node) -> FastStaNodeId
{
  const auto node_id = context.nodes.size();
  if ((node.kind == FastStaNodeKind::kBufferInput || node.kind == FastStaNodeKind::kBufferOutput) && node.inst_name.empty()) {
    CTSLOG.error(Loc::current(), "FastStaChar: buffer node must carry an explicit instance identity.");
  }
  if (node.kind == FastStaNodeKind::kBufferInput && !context.buffer_input_node_id_by_inst.emplace(node.inst_name, node_id).second) {
    CTSLOG.error(Loc::current(), "FastStaChar: duplicate buffer-input identity ", node.inst_name, ".");
  }
  if (node.kind == FastStaNodeKind::kBufferOutput && !context.buffer_output_node_id_by_inst.emplace(node.inst_name, node_id).second) {
    CTSLOG.error(Loc::current(), "FastStaChar: duplicate buffer-output identity ", node.inst_name, ".");
  }
  context.node_id_by_name[node.name] = node_id;
  context.node_id_by_location[{node.location.x_dbu, node.location.y_dbu}] = node_id;
  context.nodes.push_back(std::move(node));
  return node_id;
}

auto appendNet(FastStaClockContext& context, FastStaNet net) -> FastStaNetId
{
  const auto net_id = context.nets.size();
  context.net_id_by_name[net.name] = net_id;
  if (net.driver_node_id < context.nodes.size()) {
    context.nodes.at(net.driver_node_id).output_net_ids.push_back(net_id);
  }
  for (const auto load_node_id : net.load_node_ids) {
    if (load_node_id < context.nodes.size()) {
      context.nodes.at(load_node_id).incoming_net_id = net_id;
    }
  }
  context.nets.push_back(std::move(net));
  return net_id;
}

auto sourceOutputNodeId(const FastStaClockContext& context) -> FastStaNodeId
{
  if (context.source_node_id >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto& source_input = context.nodes.at(context.source_node_id);
  if (source_input.kind != FastStaNodeKind::kBufferInput || source_input.inst_name.empty()) {
    return kInvalidFastStaNodeId;
  }
  const auto output_iter = context.buffer_output_node_id_by_inst.find(source_input.inst_name);
  if (output_iter == context.buffer_output_node_id_by_inst.end() || output_iter->second >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto& source_output = context.nodes.at(output_iter->second);
  return source_output.kind == FastStaNodeKind::kBufferOutput && source_output.inst_name == source_input.inst_name ? output_iter->second
                                                                                                                   : kInvalidFastStaNodeId;
}

auto sinkNodeId(const FastStaClockContext& context) -> FastStaNodeId
{
  FastStaNodeId sink_node_id = kInvalidFastStaNodeId;
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (context.nodes.at(node_id).kind != FastStaNodeKind::kSink) {
      continue;
    }
    if (sink_node_id != kInvalidFastStaNodeId) {
      return kInvalidFastStaNodeId;
    }
    sink_node_id = node_id;
  }
  return sink_node_id;
}

auto observationNodeId(const FastStaClockContext& context) -> FastStaNodeId
{
  const auto sink_node_id = sinkNodeId(context);
  if (sink_node_id >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto incoming_net_id = context.nodes.at(sink_node_id).incoming_net_id;
  if (incoming_net_id >= context.nets.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto driver_node_id = context.nets.at(incoming_net_id).driver_node_id;
  const auto source_output_node_id = sourceOutputNodeId(context);
  if (driver_node_id < context.nodes.size() && driver_node_id != source_output_node_id
      && context.nodes.at(driver_node_id).kind == FastStaNodeKind::kBufferOutput) {
    return driver_node_id;
  }
  return sink_node_id;
}

auto validateCharacterizationTopology(const FastStaClockContext& context) -> std::string
{
  if (sourceOutputNodeId(context) >= context.nodes.size()) {
    return "source_buffer_pair_unavailable";
  }
  if (sinkNodeId(context) >= context.nodes.size()) {
    return "unique_sink_node_unavailable";
  }
  if (observationNodeId(context) >= context.nodes.size()) {
    return "observation_node_unavailable";
  }

  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kBufferInput && node.kind != FastStaNodeKind::kBufferOutput) {
      continue;
    }
    const auto& own_index = node.kind == FastStaNodeKind::kBufferInput ? context.buffer_input_node_id_by_inst : context.buffer_output_node_id_by_inst;
    const auto own_iter = own_index.find(node.inst_name);
    if (node.inst_name.empty() || own_iter == own_index.end() || own_iter->second != node_id) {
      return "buffer_pair_index_incomplete:" + node.inst_name;
    }
    const auto& peer_index = node.kind == FastStaNodeKind::kBufferInput ? context.buffer_output_node_id_by_inst : context.buffer_input_node_id_by_inst;
    const auto peer_iter = peer_index.find(node.inst_name);
    if (peer_iter == peer_index.end() || peer_iter->second >= context.nodes.size()) {
      return "buffer_peer_index_incomplete:" + node.inst_name;
    }
    const auto& peer_node = context.nodes.at(peer_iter->second);
    const auto expected_peer_kind = node.kind == FastStaNodeKind::kBufferInput ? FastStaNodeKind::kBufferOutput : FastStaNodeKind::kBufferInput;
    if (peer_node.kind != expected_peer_kind || peer_node.inst_name != node.inst_name) {
      return "buffer_peer_index_invalid:" + node.inst_name;
    }
  }
  return {};
}

auto sourceBoundaryNetId(const FastStaClockContext& context) -> FastStaNetId
{
  const auto source_output_id = sourceOutputNodeId(context);
  if (source_output_id >= context.nodes.size() || context.nodes.at(source_output_id).output_net_ids.empty()) {
    return kInvalidFastStaNetId;
  }
  return context.nodes.at(source_output_id).output_net_ids.front();
}

auto makeLinearParasitic(const FastStaClockContext& context, const FastStaNet& net, FastStaNodeId driver_node_id, FastStaNodeId load_node_id,
                         double wirelength_um) -> FastStaNetParasitic
{
  if (context.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastStaChar: Wrapper is unavailable.");
  }
  const auto wire_cap_profile = context.wrapper->queryRequiredClockTimingWireCapacitanceProfile(context.routing_layer, wirelength_um, context.wire_width_um);
  const auto wire_cap_pf = wire_cap_profile.total_cap_pf;
  const auto driver_wire_cap_pf = wire_cap_profile.timing_effective_cap_pf;
  const auto wire_resistance_ohm = context.wrapper->queryRequiredWireResistance(context.routing_layer, wirelength_um, context.wire_width_um);
  FastStaNetParasitic parasitic;
  parasitic.rc_nodes.push_back(FastStaRcNode{
      .name = net.name + "@root",
      .ground_cap_pf = wire_cap_profile.ground_cap_pf / 2.0,
      .coupling_cap_pf = wire_cap_profile.coupling_cap_pf / 2.0,
      .wire_cap_pf = wire_cap_pf / 2.0,
      .driver_wire_cap_pf = driver_wire_cap_pf / 2.0,
      .terminal_node_id = driver_node_id,
  });
  parasitic.rc_nodes.push_back(FastStaRcNode{
      .name = net.name + "@load",
      .ground_cap_pf = wire_cap_profile.ground_cap_pf / 2.0,
      .coupling_cap_pf = wire_cap_profile.coupling_cap_pf / 2.0,
      .wire_cap_pf = wire_cap_pf / 2.0,
      .driver_wire_cap_pf = driver_wire_cap_pf / 2.0,
      .terminal_node_id = load_node_id,
  });
  parasitic.rc_edges.push_back(FastStaRcEdge{
      .from = 0U,
      .to = 1U,
      .resistance_ohm = wire_resistance_ohm,
      .ground_capacitance_pf = wire_cap_profile.ground_cap_pf,
      .coupling_capacitance_pf = wire_cap_profile.coupling_cap_pf,
      .capacitance_pf = wire_cap_pf,
      .timing_coupling_factor = wire_cap_profile.timing_coupling_factor,
      .driver_capacitance_pf = driver_wire_cap_pf,
  });
  parasitic.ground_cap_pf = wire_cap_profile.ground_cap_pf;
  parasitic.coupling_cap_pf = wire_cap_profile.coupling_cap_pf;
  parasitic.timing_coupling_factor = wire_cap_profile.timing_coupling_factor;
  parasitic.rc_node_id_by_name = {{parasitic.rc_nodes.front().name, 0U}, {parasitic.rc_nodes.back().name, 1U}};
  parasitic.root_rc_node_id = 0U;
  return parasitic;
}

auto rootTiming(const FastStaClockContext& context) -> FastStaTimingPoint
{
  const auto source_output_id = sourceOutputNodeId(context);
  if (source_output_id >= context.nodes.size()) {
    return {};
  }
  return context.nodes.at(source_output_id).timing;
}

auto isCharacterizedBufferOutput(const FastStaClockContext& context, FastStaNodeId node_id) -> bool
{
  return node_id < context.nodes.size() && node_id != sourceOutputNodeId(context) && context.nodes.at(node_id).kind == FastStaNodeKind::kBufferOutput;
}

auto selectedBufferInternalPower(const FastStaClockContext& context) -> double
{
  double power_w = 0.0;
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (isCharacterizedBufferOutput(context, node_id)) {
      power_w += context.nodes.at(node_id).internal_power_w;
    }
  }
  return power_w;
}

auto selectedBufferLeakagePower(const FastStaClockContext& context) -> double
{
  double power_w = 0.0;
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (isCharacterizedBufferOutput(context, node_id)) {
      power_w += context.nodes.at(node_id).leakage_power_w;
    }
  }
  return power_w;
}

}  // namespace

auto FastStaChar::buildContext(const FastStaCharTopologySpec& spec) -> BuildResult
{
  FastStaClockContext context;
  if (spec.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastStaChar: Wrapper must be provided.");
  }
  const auto dbu_per_um = resolveDbuPerUm(spec);
  if (spec.routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastStaChar: routing layer must be explicitly provided.");
  }
  context.clock_name = "cts_char_clk";
  context.wrapper = spec.wrapper;
  context.clock_net_name = "cts_char_net_0";
  context.clock_period_ns = spec.clock_period_ns;
  context.root_input_slew_ns = std::max(0.0, spec.root_input_slew_ns);
  context.dbu_per_um = dbu_per_um;
  context.routing_layer = spec.routing_layer;
  context.wire_width_um = spec.wire_width_um;

  std::vector<std::string> required_cell_masters{spec.source_cell_master, spec.sink_cell_master};
  required_cell_masters.insert(required_cell_masters.end(), spec.buffer_cell_masters.begin(), spec.buffer_cell_masters.end());
  for (const auto& cell_master : required_cell_masters) {
    if (context.liberty_cell_by_master.contains(cell_master)) {
      continue;
    }
    const auto liberty_cell = FastStaLiberty::extractBufferCell(*spec.wrapper, cell_master);
    if (!liberty_cell.has_value()) {
      return BuildResult{.failure_reason = "liberty_cell_unavailable:" + cell_master};
    }
    context.liberty_cell_by_master.emplace(cell_master, *liberty_cell);
  }

  const auto source_input = appendNode(context, FastStaNode{
                                                    .kind = FastStaNodeKind::kBufferInput,
                                                    .name = "cts_char_source/A",
                                                    .inst_name = "cts_char_source",
                                                    .pin_name = "A",
                                                    .cell_master = spec.source_cell_master,
                                                    .location = makePoint(0.0, context.dbu_per_um),
                                                    .input_cap_pf = context.liberty_cell_by_master.at(spec.source_cell_master).input_cap_pf,
                                                    .max_slew_ns = context.liberty_cell_by_master.at(spec.source_cell_master).input_slew_limit_ns,
                                                    .output_net_ids = {},
                                                    .timing = {},
                                                });
  const auto source_output = appendNode(context, FastStaNode{
                                                     .kind = FastStaNodeKind::kBufferOutput,
                                                     .name = "cts_char_source/Y",
                                                     .inst_name = "cts_char_source",
                                                     .pin_name = "Y",
                                                     .cell_master = spec.source_cell_master,
                                                     .location = makePoint(0.0, context.dbu_per_um),
                                                     .output_net_ids = {},
                                                     .timing = {},
                                                 });
  context.source_node_id = source_input;

  auto current_x_um = 0.0;
  auto driver_node_id = source_output;
  for (std::size_t segment_index = 0U; segment_index < spec.wire_segments_um.size(); ++segment_index) {
    current_x_um += std::max(0.0, spec.wire_segments_um.at(segment_index));
    FastStaNodeId load_node_id = kInvalidFastStaNodeId;
    FastStaNodeId next_driver_node_id = kInvalidFastStaNodeId;
    if (segment_index < spec.buffer_cell_masters.size()) {
      const auto& cell_master = spec.buffer_cell_masters.at(segment_index);
      const auto input_name = "cts_char_buf_" + std::to_string(segment_index) + "/A";
      const auto output_name = "cts_char_buf_" + std::to_string(segment_index) + "/Y";
      const auto inst_name = "cts_char_buf_" + std::to_string(segment_index);
      load_node_id = appendNode(context, FastStaNode{
                                             .kind = FastStaNodeKind::kBufferInput,
                                             .name = input_name,
                                             .inst_name = inst_name,
                                             .pin_name = "A",
                                             .cell_master = cell_master,
                                             .location = makePoint(current_x_um, context.dbu_per_um),
                                             .input_cap_pf = context.liberty_cell_by_master.at(cell_master).input_cap_pf,
                                             .max_slew_ns = context.liberty_cell_by_master.at(cell_master).input_slew_limit_ns,
                                             .output_net_ids = {},
                                             .timing = {},
                                         });
      next_driver_node_id = appendNode(context, FastStaNode{
                                                    .kind = FastStaNodeKind::kBufferOutput,
                                                    .name = output_name,
                                                    .inst_name = inst_name,
                                                    .pin_name = "Y",
                                                    .cell_master = cell_master,
                                                    .location = makePoint(current_x_um, context.dbu_per_um),
                                                    .output_net_ids = {},
                                                    .timing = {},
                                                });
    } else {
      load_node_id = appendNode(context, FastStaNode{
                                             .kind = FastStaNodeKind::kSink,
                                             .name = "cts_char_sink/A",
                                             .inst_name = "cts_char_sink",
                                             .pin_name = "A",
                                             .cell_master = spec.sink_cell_master,
                                             .location = makePoint(current_x_um, context.dbu_per_um),
                                             .input_cap_pf = context.liberty_cell_by_master.at(spec.sink_cell_master).input_cap_pf,
                                             .max_slew_ns = context.liberty_cell_by_master.at(spec.sink_cell_master).input_slew_limit_ns,
                                             .output_net_ids = {},
                                             .timing = {},
                                         });
    }

    const auto net_name = "cts_char_net_" + std::to_string(segment_index);
    const auto net_id
        = appendNet(context, FastStaNet{
                                 .name = net_name,
                                 .driver_node_id = driver_node_id,
                                 .load_node_ids = {load_node_id},
                                 .max_cap_pf = context.liberty_cell_by_master.at(context.nodes.at(driver_node_id).cell_master).output_cap_limit_pf,
                                 .parasitic = {},
                                 .driver_dmp = {},
                             });
    context.nets.at(net_id).parasitic
        = makeLinearParasitic(context, context.nets.at(net_id), driver_node_id, load_node_id, std::max(0.0, spec.wire_segments_um.at(segment_index)));

    if (segment_index < spec.buffer_cell_masters.size()) {
      if (next_driver_node_id >= context.nodes.size()) {
        return BuildResult{.failure_reason = "buffer_output_node_unavailable:" + context.nodes.at(load_node_id).inst_name};
      }
      driver_node_id = next_driver_node_id;
    }
  }

  FastStaParasitics::updateNetLoads(context);
  if (const auto topology_error = validateCharacterizationTopology(context); !topology_error.empty()) {
    return BuildResult{.failure_reason = std::move(topology_error)};
  }
  return BuildResult{.context = std::move(context), .failure_reason = {}};
}

auto FastStaChar::setLoad(FastStaClockContext& context, double effective_load_pf) -> bool
{
  const auto sink_id = sinkNodeId(context);
  if (sink_id >= context.nodes.size()) {
    return false;
  }
  const auto& sink_node = context.nodes.at(sink_id);
  const auto sink_cell_iter = context.liberty_cell_by_master.find(sink_node.cell_master);
  if (sink_cell_iter == context.liberty_cell_by_master.end()) {
    CTSLOG.error(Loc::current(), "FastStaChar: committed characterization context is missing sink Liberty data for ", sink_node.cell_master, ".");
  }
  const auto sink_input_cap_pf = sink_cell_iter->second.input_cap_pf;
  context.nodes.at(sink_id).input_cap_pf = sink_input_cap_pf + std::max(0.0, effective_load_pf);
  FastStaParasitics::updateNetLoads(context);
  context.timing_valid = false;
  context.power_valid = false;
  return true;
}

auto FastStaChar::runSample(FastStaClockContext& context, double input_slew_ns) -> FastStaCharSampleResult
{
  context.root_input_slew_ns = std::max(0.0, input_slew_ns);
  const auto timing_updated = FastStaTiming::update(context);
  if (!timing_updated) {
    return {};
  }

  const auto source_output_timing = rootTiming(context);
  if (!source_output_timing.valid) {
    return {};
  }

  const auto observation_id = observationNodeId(context);
  if (observation_id >= context.nodes.size() || !context.nodes.at(observation_id).timing.valid) {
    return {};
  }

  if (!FastStaPower::update(context)) {
    return {};
  }
  const auto boundary_net_id = sourceBoundaryNetId(context);
  if (boundary_net_id >= context.nets.size()) {
    return {};
  }
  const auto boundary_power = context.nets.at(boundary_net_id).switching_power_w;
  const auto delay_ns = std::max(0.0, context.nodes.at(observation_id).timing.arrival_ns - source_output_timing.arrival_ns);
  return FastStaCharSampleResult{
      .valid = true,
      .delay_ns = delay_ns,
      .output_slew_ns = context.nodes.at(observation_id).timing.slew_ns,
      .power_w = selectedBufferInternalPower(context) + selectedBufferLeakagePower(context) + context.power.switching_power_w,
      .source_boundary_net_switch_power_w = boundary_power,
  };
}

}  // namespace icts
