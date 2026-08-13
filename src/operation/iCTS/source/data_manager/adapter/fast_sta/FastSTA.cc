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
 * @file FastSTA.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS-facing facade implementation for fast timing and power calculation.
 */

#include "FastSTA.hh"

#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>

#include "FastSTABuilder.hh"
#include "FastSTAChar.hh"
#include "FastSTAIncremental.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "Logger.hh"
#include "clock_net_parasitic/FastSTAClockNetParasitic.hh"
#include "clock_state/FastSTAClockState.hh"
#include "design/Clock.hh"
#include "design/Net.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts {
namespace {

auto requireEnvironment(const std::optional<FastStaEnvironment>& environment) -> const FastStaEnvironment&
{
  if (!environment.has_value()) {
    CTSLOG.error(Loc::current(), "FastSTA: runtime environment is not bound.");
  }
  if (environment->wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastSTA: bound Wrapper is null.");
  }
  if (environment->dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "FastSTA: bound DBU-per-micron is invalid.");
  }
  if (environment->routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastSTA: bound routing layer is invalid.");
  }
  return *environment;
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

auto makeClockGraphProfile(const FastStaClockContext& context) -> FastStaClockGraphProfile
{
  FastStaClockGraphProfile profile;
  profile.node_count = context.nodes.size();
  profile.net_count = context.nets.size();
  for (const auto& node : context.nodes) {
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
  return profile;
}

auto makeClockTreeTopology(const FastStaClockContext& context) -> FastStaClockTreeTopology
{
  FastStaClockTreeTopology topology;
  topology.source_node_id = context.source_node_id;
  topology.parent_by_node.assign(context.nodes.size(), kInvalidFastStaNodeId);

  const auto find_buffer_input = [&](const std::string& inst_name) -> FastStaNodeId {
    if (const auto indexed = context.buffer_input_node_id_by_inst.find(inst_name); indexed != context.buffer_input_node_id_by_inst.end()) {
      if (indexed->second < context.nodes.size()) {
        const auto& node = context.nodes.at(indexed->second);
        if (node.kind == FastStaNodeKind::kBufferInput && node.inst_name == inst_name) {
          return indexed->second;
        }
      }
      return kInvalidFastStaNodeId;
    }
    for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
      const auto& node = context.nodes.at(node_id);
      if (node.kind == FastStaNodeKind::kBufferInput && node.inst_name == inst_name) {
        return node_id;
      }
    }
    return kInvalidFastStaNodeId;
  };

  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.kind == FastStaNodeKind::kBufferOutput) {
      const auto input_node_id = find_buffer_input(node.inst_name);
      if (input_node_id != kInvalidFastStaNodeId) {
        topology.parent_by_node.at(node_id) = input_node_id;
      }
      continue;
    }
    if (node.incoming_net_id < context.nets.size()) {
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

struct FastSTA::ContextStore
{
  std::vector<std::unique_ptr<FastStaClockContext>> clock_contexts;
  std::vector<bool> clock_context_valid;
  std::vector<std::unique_ptr<FastStaClockContext>> char_contexts;
  std::vector<bool> char_context_valid;
};

FastSTA::FastSTA() : _contexts(std::make_unique<ContextStore>())
{
}

FastSTA::~FastSTA() = default;

auto FastSTA::bindEnvironment(const FastStaEnvironment& environment) -> void
{
  if (environment.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastSTA: cannot bind a null Wrapper.");
  }
  if (environment.dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "FastSTA: cannot bind invalid DBU-per-micron.");
  }
  if (environment.routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastSTA: cannot bind invalid routing layer.");
  }
  _environment = environment;
}

auto FastSTA::buildClockContext(const FastStaClockBuildInput& input) -> FastStaClockBuildResult
{
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "FastSTA: clock context build input has no clock.");
  }
  const auto& environment = requireEnvironment(_environment);
  auto build = FastStaBuilder::buildClockContext(environment, input);
  if (!build.context.has_value()) {
    return FastStaClockBuildResult{.failure_reason = std::move(build.failure_reason)};
  }
  auto context = std::make_unique<FastStaClockContext>(std::move(build.context).value());
  if (!FastStaTiming::update(*context)) {
    return FastStaClockBuildResult{.failure_reason = "timing_analysis_unavailable:" + input.clock->get_clock_name()};
  }
  if (!FastStaPower::update(*context)) {
    return FastStaClockBuildResult{.failure_reason = "power_analysis_unavailable:" + input.clock->get_clock_name()};
  }

  const auto clock_id = _contexts->clock_contexts.size();
  _contexts->clock_contexts.push_back(std::move(context));
  _contexts->clock_context_valid.push_back(true);
  return FastStaClockBuildResult{.clock_id = clock_id, .failure_reason = {}};
}

auto FastSTA::eraseClockContext(FastStaClockId clock_id) -> bool
{
  if (clock_id >= _contexts->clock_contexts.size() || clock_id >= _contexts->clock_context_valid.size() || !_contexts->clock_context_valid.at(clock_id)
      || _contexts->clock_contexts.at(clock_id) == nullptr) {
    return false;
  }
  _contexts->clock_context_valid.at(clock_id) = false;
  _contexts->clock_contexts.at(clock_id).reset();
  return true;
}

auto FastSTA::reset() -> void
{
  _contexts->clock_contexts.clear();
  _contexts->clock_context_valid.clear();
  _contexts->char_contexts.clear();
  _contexts->char_context_valid.clear();
  _environment = std::nullopt;
}

auto FastSTA::buildCharContext(const FastStaCharTopologySpec& spec) -> FastStaCharBuildResult
{
  auto build = FastStaChar::buildContext(spec);
  if (!build.context.has_value()) {
    return FastStaCharBuildResult{.failure_reason = std::move(build.failure_reason)};
  }
  const auto char_context_id = _contexts->char_contexts.size();
  _contexts->char_contexts.push_back(std::make_unique<FastStaClockContext>(std::move(build.context).value()));
  _contexts->char_context_valid.push_back(true);
  return FastStaCharBuildResult{.context_id = char_context_id, .failure_reason = {}};
}

auto FastSTA::eraseCharContext(FastStaCharContextId char_context_id) -> bool
{
  if (char_context_id >= _contexts->char_contexts.size() || char_context_id >= _contexts->char_context_valid.size()
      || !_contexts->char_context_valid.at(char_context_id) || _contexts->char_contexts.at(char_context_id) == nullptr) {
    return false;
  }
  _contexts->char_context_valid.at(char_context_id) = false;
  _contexts->char_contexts.at(char_context_id).reset();
  return true;
}

auto FastSTA::setCharLoad(FastStaCharContextId char_context_id, double effective_load_pf) -> bool
{
  if (char_context_id >= _contexts->char_contexts.size() || char_context_id >= _contexts->char_context_valid.size()
      || !_contexts->char_context_valid.at(char_context_id) || _contexts->char_contexts.at(char_context_id) == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: characterization load update skipped because char context id is invalid.");
    return false;
  }
  return FastStaChar::setLoad(*_contexts->char_contexts.at(char_context_id), effective_load_pf);
}

auto FastSTA::runCharSample(FastStaCharContextId char_context_id, double input_slew_ns) -> FastStaCharSampleResult
{
  if (char_context_id >= _contexts->char_contexts.size() || char_context_id >= _contexts->char_context_valid.size()
      || !_contexts->char_context_valid.at(char_context_id) || _contexts->char_contexts.at(char_context_id) == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: characterization sample skipped because char context id is invalid.");
    return {};
  }
  return FastStaChar::runSample(*_contexts->char_contexts.at(char_context_id), input_slew_ns);
}

auto FastSTA::changeBufferMasters(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: buffer master batch change skipped because clock context id is invalid.");
    return false;
  }
  if (changes.empty()) {
    return context->timing_valid && context->power_valid;
  }
  if (!FastStaIncremental::changeBufferMasters(*context, changes)) {
    return false;
  }
  return FastStaTiming::update(*context) && FastStaPower::update(*context);
}

auto FastSTA::changeBufferMastersTimingOnly(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: timing-only buffer master batch change skipped because clock context id is invalid.");
    return false;
  }
  if (changes.empty()) {
    return context->timing_valid;
  }
  const auto dirty_region = FastStaIncremental::changeBufferMastersIncremental(*context, changes);
  if (!dirty_region.has_value() || !FastStaTiming::updateRegion(*context, *dirty_region)) {
    context->timing_valid = false;
    context->power_valid = false;
    return false;
  }
  context->power_valid = false;
  return context->timing_valid;
}

auto FastSTA::updateTiming(FastStaClockId clock_id) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: timing update skipped because clock context id is invalid.");
    return false;
  }
  return FastStaTiming::update(*context);
}

auto FastSTA::updatePower(FastStaClockId clock_id) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: power update skipped because clock context id is invalid.");
    return false;
  }
  return FastStaPower::update(*context);
}

auto FastSTA::injectNetRouteTree(FastStaClockId clock_id, const Net& net, const ClockSteinerTree<int>& route_tree, FastStaClockNetRcTreeCounts& rc_tree_counts)
    -> bool
{
  rc_tree_counts = {};
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: route-tree injection skipped because clock context id is invalid.");
    return false;
  }
  if (!FastStaBuilder::injectNetRouteTree(*context, net, route_tree)) {
    return false;
  }
  const auto net_iter = context->net_id_by_name.find(net.get_name());
  if (net_iter == context->net_id_by_name.end() || net_iter->second >= context->nets.size()) {
    return true;
  }
  const auto& parasitic = context->nets.at(net_iter->second).parasitic;
  rc_tree_counts.rc_node_count = parasitic.rc_nodes.size();
  rc_tree_counts.rc_edge_count = parasitic.rc_edges.size();
  return true;
}

auto FastSTA::queryClockGraphProfile(FastStaClockId clock_id) const -> std::optional<FastStaClockGraphProfile>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  return makeClockGraphProfile(*context);
}

auto FastSTA::queryClockAnalysisStatus(FastStaClockId clock_id) const -> std::optional<FastStaClockAnalysisStatus>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  return FastStaClockAnalysisStatus{.timing_valid = context->timing_valid, .power_valid = context->power_valid};
}

auto FastSTA::queryClockTreeTopology(FastStaClockId clock_id) const -> std::optional<FastStaClockTreeTopology>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  return makeClockTreeTopology(*context);
}

auto FastSTA::collectClockSizingBuffers(FastStaClockId clock_id) const -> std::vector<FastStaClockSizingBuffer>
{
  std::vector<FastStaClockSizingBuffer> buffers;
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr) {
    return buffers;
  }
  buffers.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kBufferOutput || node.inst_name.empty() || node.cell_master.empty()) {
      continue;
    }
    buffers.push_back(FastStaClockSizingBuffer{.node_id = node_id, .inst_name = node.inst_name, .cell_master = node.cell_master});
  }
  return buffers;
}

auto FastSTA::collectClockSinkArrivals(FastStaClockId clock_id) const -> std::vector<FastStaClockSinkArrival>
{
  std::vector<FastStaClockSinkArrival> sinks;
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr) {
    return sinks;
  }
  sinks.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kSink || !node.timing.valid) {
      continue;
    }
    sinks.push_back(FastStaClockSinkArrival{.node_id = node_id, .sink_name = node.name, .arrival_ns = node.timing.arrival_ns});
  }
  return sinks;
}

auto FastSTA::queryClockNodeArrival(FastStaClockId clock_id, FastStaNodeId node_id) const -> std::optional<double>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || node_id >= context->nodes.size() || !context->nodes.at(node_id).timing.valid) {
    return std::nullopt;
  }
  return context->nodes.at(node_id).timing.arrival_ns;
}

auto FastSTA::querySkew(FastStaClockId clock_id) const -> FastStaSkewSummary
{
  const auto* context = queryClockContext(clock_id);
  return context == nullptr ? FastStaSkewSummary{} : context->skew;
}

auto FastSTA::queryCapStatus(FastStaClockId clock_id, FastStaNetId net_id) const -> std::optional<FastStaCapStatus>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || net_id >= context->nets.size()) {
    return std::nullopt;
  }
  const auto& net = context->nets.at(net_id);
  return FastStaCapStatus{.net_id = net_id,
                          .net_name = net.name,
                          .load_cap_pf = net.load_cap_pf,
                          .max_cap_pf = net.max_cap_pf,
                          .violated = net.max_cap_pf > 0.0 && net.load_cap_pf > net.max_cap_pf};
}

auto FastSTA::querySlewStatus(FastStaClockId clock_id, FastStaNodeId node_id) const -> std::optional<FastStaSlewStatus>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || node_id >= context->nodes.size()) {
    return std::nullopt;
  }
  const auto& node = context->nodes.at(node_id);
  if (!node.timing.valid) {
    return std::nullopt;
  }
  return FastStaSlewStatus{.node_id = node_id,
                           .node_name = node.name,
                           .role = toSlewRole(node.kind),
                           .slew_ns = node.timing.slew_ns,
                           .max_slew_ns = node.max_slew_ns,
                           .violated = node.max_slew_ns > 0.0 && node.timing.slew_ns > node.max_slew_ns};
}

auto FastSTA::queryPower(FastStaClockId clock_id) const -> std::optional<FastStaPowerSummary>
{
  const auto* context = queryClockContext(clock_id);
  return context == nullptr || !context->power_valid ? std::nullopt : std::optional<FastStaPowerSummary>{context->power};
}

auto FastSTA::queryClockContext(FastStaClockId clock_id) const -> const FastStaClockContext*
{
  if (clock_id >= _contexts->clock_contexts.size() || clock_id >= _contexts->clock_context_valid.size() || !_contexts->clock_context_valid.at(clock_id)
      || _contexts->clock_contexts.at(clock_id) == nullptr) {
    return nullptr;
  }
  return _contexts->clock_contexts.at(clock_id).get();
}

auto FastSTA::mutableClockContext(FastStaClockId clock_id) -> FastStaClockContext*
{
  if (clock_id >= _contexts->clock_contexts.size() || clock_id >= _contexts->clock_context_valid.size() || !_contexts->clock_context_valid.at(clock_id)
      || _contexts->clock_contexts.at(clock_id) == nullptr) {
    return nullptr;
  }
  return _contexts->clock_contexts.at(clock_id).get();
}

}  // namespace icts
