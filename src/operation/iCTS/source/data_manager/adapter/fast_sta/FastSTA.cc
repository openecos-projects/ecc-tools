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

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTABuilder.hh"
#include "FastSTAChar.hh"
#include "FastSTAIncremental.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "Logger.hh"
#include "clock_state/FastSTAContextStore.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "timing/FastSTAConstraints.hh"

namespace icts {
namespace {

auto requireEnvironment(const std::optional<FastStaEnvironment>& environment) -> const FastStaEnvironment*
{
  if (!environment.has_value()) {
    CTSLOG.error(Loc::current(), "FastSTA: runtime environment is not bound.");
    return nullptr;
  }
  if (environment->wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "FastSTA: bound Wrapper is null.");
    return nullptr;
  }
  if (environment->dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "FastSTA: bound DBU-per-micron is invalid.");
    return nullptr;
  }
  if (environment->routing_layer <= 0) {
    CTSLOG.error(Loc::current(), "FastSTA: bound routing layer is invalid.");
    return nullptr;
  }
  return &*environment;
}

}  // namespace

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

auto FastSTA::buildContext(const FastStaBuildInput& input) -> FastStaBuildResult
{
  if (input.clock == nullptr && input.timing_graph == nullptr) {
    return FastStaBuildResult{.failure_reason = "clock_and_timing_graph_unavailable"};
  }
  const auto* environment = requireEnvironment(_environment);
  if (environment == nullptr) {
    return FastStaBuildResult{.failure_reason = "fast_sta_environment_unavailable"};
  }
  auto build = FastStaBuilder::buildContext(*environment, input);
  if (!build.context.has_value()) {
    return FastStaBuildResult{.failure_reason = std::move(build.failure_reason)};
  }
  auto context = std::make_unique<FastStaContext>(std::move(build.context).value());
  if (!input.defer_timing_update) {
    if (!FastStaTiming::update(*context)) {
      const auto detail = context->timing_summary.fallback_reason.empty() ? context->clock_name : context->timing_summary.fallback_reason;
      return FastStaBuildResult{.failure_reason = "timing_analysis_unavailable:" + detail};
    }
    if (input.require_power && !FastStaPower::update(*context)) {
      return FastStaBuildResult{.failure_reason = "power_analysis_unavailable:" + context->clock_name};
    }
  }

  const auto context_id = _contexts->contexts.size();
  _contexts->contexts.push_back(std::move(context));
  _contexts->context_valid.push_back(true);
  return FastStaBuildResult{.context_id = context_id, .failure_reason = {}};
}

auto FastSTA::buildClockSizingContext(FastStaContextId source_context_id, const FastStaBuildInput& input) -> FastStaBuildResult
{
  const auto* source = queryContext(source_context_id);
  if (source == nullptr) {
    return FastStaBuildResult{.failure_reason = "clock_sizing_source_context_unavailable"};
  }
  auto build = FastStaBuilder::extractClockContext(*source, input);
  if (!build.context.has_value()) {
    return {.failure_reason = std::move(build.failure_reason)};
  }
  auto context = std::make_unique<FastStaContext>(std::move(*build.context));
  context->skew = FastStaTiming::calcSkew(*context);
  context->timing_summary.max_skew_ns = context->skew.valid ? context->skew.skew_ns : 0.0;
  if (input.require_power) {
    // Extraction copies the unchanged per-node/net power with its timing.
    // Rebuild only the local aggregate when those source facts are valid.
    const bool power_ready
        = source->power_valid ? FastStaPower::updatePreparedRegion(
                                    *context, FastStaDirtyRegion{.valid = true, .node_ids = {}, .net_ids = {}, .load_update_net_ids = {}, .start_node_ids = {}})
                              : FastStaPower::update(*context);
    if (!power_ready) {
      return {.failure_reason = "clock_sizing_power_unavailable"};
    }
  }
  const auto id = _contexts->contexts.size();
  _contexts->contexts.push_back(std::move(context));
  _contexts->context_valid.push_back(true);
  return {.context_id = id, .failure_reason = {}};
}

auto FastSTA::replaceContext(FastStaContextId context_id, const FastStaBuildInput& input) -> FastStaBuildResult
{
  if (_contexts->pending_by_committed.contains(context_id) || _contexts->transactions.contains(context_id) || context_id >= _contexts->contexts.size()
      || !_contexts->context_valid.at(context_id) || _contexts->contexts.at(context_id) == nullptr) {
    return FastStaBuildResult{.failure_reason = "context_to_replace_unavailable"};
  }
  auto candidate = buildContext(input);
  if (!candidate.ok()) {
    return candidate;
  }
  (void) eraseContext(context_id);
  return candidate;
}

auto FastSTA::synchronizeClockContext(FastStaContextId pending_id, const FastStaBuildInput& input) -> FastStaBuildResult
{
  auto* context = mutableContext(pending_id);
  const auto transaction = _contexts->transactions.find(pending_id);
  if (context == nullptr || transaction == _contexts->transactions.end() || !transaction->second.edits.empty() || input.constraints == nullptr) {
    return {.failure_reason = "clock_topology_transaction_unavailable"};
  }
  if (context->input_constraints != *input.constraints || context->propagate_all_clocks != input.propagate_all_clocks) {
    return {.failure_reason = "clock_topology_constraint_input_changed"};
  }
  if (context->owner_clock_scope_available != (input.clock != nullptr)
      || (input.clock != nullptr
          && (context->clock_name != input.clock->get_clock_name() || context->clock_net_name != input.clock->get_clock_net_name()
              || context->clock_period_ns != input.clock->get_clock_period_ns() || context->source_node_id >= context->nodes.size()
              || context->nodes.at(context->source_node_id).name != Design::getPinFullName(input.clock->get_clock_source())))) {
    return {.failure_reason = "clock_topology_clock_input_changed"};
  }
  if (FastStaBuilder::matchesClockInput(*context, input)) {
    if (input.require_power && !context->power_valid) {
      return {.failure_reason = "clock_topology_complete_power_unavailable"};
    }
    return {.context_id = pending_id, .failure_reason = {}};
  }
  auto clock_input = input;
  clock_input.timing_graph = nullptr;
  auto build = FastStaBuilder::buildContext(*requireEnvironment(_environment), clock_input, context);
  if (!build.context.has_value()) {
    return {.failure_reason = std::move(build.failure_reason)};
  }
  std::unordered_set<FastStaNodeId> nodes(context->clock_overlay_node_ids.begin(), context->clock_overlay_node_ids.end());
  std::unordered_set<FastStaNetId> nets(context->clock_overlay_net_ids.begin(), context->clock_overlay_net_ids.end());
  for (FastStaNodeId id = 0U; id < context->nodes.size(); ++id) {
    if (context->nodes.at(id).domain == FastStaNodeDomain::kClock) {
      nodes.insert(id);
    }
  }
  for (FastStaNetId id = 0U; id < context->nets.size(); ++id) {
    if (context->nets.at(id).domain == FastStaNetDomain::kClock) {
      nets.insert(id);
    }
  }
  for (const auto& node : build.context->nodes) {
    if (const auto found = context->node_id_by_name.find(node.name); found != context->node_id_by_name.end()) {
      nodes.insert(found->second);
    }
  }
  FastStaDirtyRegion before_region{
      .valid = true, .node_ids = {nodes.begin(), nodes.end()}, .net_ids = {nets.begin(), nets.end()}, .load_update_net_ids = {}, .start_node_ids = {}};
  auto affected = FastStaTiming::collectAffectedLogicNodes(*context, before_region);
  if (!affected.has_value()) {
    return {.failure_reason = "clock_topology_prior_logic_dependencies_invalid"};
  }
  std::erase_if(before_region.node_ids, [&](auto id) -> bool { return id >= context->input_node_count; });
  std::erase_if(before_region.net_ids, [&](auto id) -> bool { return id >= context->input_net_count; });
  std::erase_if(*affected, [&](auto id) -> bool { return id >= context->input_node_count; });
  ContextStore::TimingEditSnapshot original(*context, {}, &before_region, &*affected);
  original.topology = std::make_unique<ContextStore::ClockTopologySnapshot>(*context, nodes);
  // A topology update publishes complete clock power. Preserve all untouched
  // power scalars as well when the preceding context had not requested power.
  std::erase_if(original.additional_node_power, [&](const auto& entry) -> bool { return entry.first >= context->input_node_count; });
  std::erase_if(original.additional_net_power, [&](const auto& entry) -> bool { return entry.first >= context->input_net_count; });
  if (context->power_valid) {
    for (FastStaNodeId id = 0U; id < context->input_node_count; ++id) {
      if (!nodes.contains(id)) {
        const auto& node = context->nodes.at(id);
        original.additional_node_power.push_back({id, {node.internal_power_w, node.leakage_power_w, node.area_um2}});
      }
    }
    for (FastStaNetId id = 0U; id < context->input_net_count; ++id) {
      if (!nets.contains(id)) {
        original.additional_net_power.emplace_back(id, context->nets.at(id).switching_power_w);
      }
    }
  }
  std::vector<FastStaNetDomain> old_net_domains;
  old_net_domains.reserve(context->input_net_count);
  for (FastStaNetId id = 0U; id < context->input_net_count; ++id) {
    old_net_domains.push_back(context->nets.at(id).domain);
  }
  auto failure = FastStaBuilder::spliceClockContext(*context, std::move(*build.context), *input.constraints);
  if (!failure.has_value()) {
    failure = FastStaConstraints::prepare(*context);
  }
  // Domain preparation can discover clock-as-data dependencies. Preserve any
  // newly reached original net before clock propagation mutates its reductions.
  for (FastStaNetId id = 0U; id < context->input_net_count; ++id) {
    if (!nets.contains(id) && context->nets.at(id).domain != old_net_domains.at(id)) {
      original.nets.emplace_back(id, context->nets.at(id));
      original.nets.back().second.domain = old_net_domains.at(id);
      nets.insert(id);
      std::erase_if(original.additional_net_power, [&](const auto& entry) -> bool { return entry.first == id; });
    }
  }
  if (failure.has_value()) {
    original.swap(*context);
    return {.failure_reason = *failure};
  }
  context->logic_preparation.reset();
  FastStaDirtyRegion region{.valid = true, .node_ids = {}, .net_ids = {}, .load_update_net_ids = {}, .start_node_ids = {}};
  for (FastStaNodeId id = 0U; id < context->nodes.size(); ++id) {
    if (context->nodes.at(id).domain == FastStaNodeDomain::kClock || (id < context->input_node_count && nodes.contains(id))) {
      region.node_ids.push_back(id);
    }
  }
  for (FastStaNetId id = 0U; id < context->nets.size(); ++id) {
    if (context->nets.at(id).domain == FastStaNetDomain::kClock) {
      region.net_ids.push_back(id);
    }
  }
  const auto next_affected = FastStaTiming::collectAffectedLogicNodes(*context, region);
  if (!next_affected.has_value()) {
    original.swap(*context);
    return {.failure_reason = "clock_topology_logic_dependencies_invalid"};
  }
  std::unordered_set<FastStaNodeId> saved_logic(affected->begin(), affected->end());
  for (const auto id : *next_affected) {
    if (id < context->input_node_count && !nodes.contains(id) && saved_logic.insert(id).second) {
      original.logic.emplace_back(id, context->nodes.at(id));
    }
  }
  if (!FastStaTiming::updateClockTopology(*context, region) || (input.require_power && !FastStaPower::update(*context))) {
    const auto reason = context->timing_summary.fallback_reason;
    original.swap(*context);
    return {.failure_reason = "clock_topology_analysis_failed:" + reason};
  }
  transaction->second.edits.push_back(std::move(original));
  return {.context_id = pending_id, .failure_reason = {}};
}

auto FastSTA::matchesClockInput(FastStaContextId context_id, const FastStaBuildInput& input) const -> bool
{
  const auto* context = queryContext(context_id);
  return context != nullptr && context->timing_valid && FastStaBuilder::matchesClockInput(*context, input);
}

auto FastSTA::changeInstanceMasters(FastStaContextId context_id, const std::vector<FastStaInstanceMasterChange>& changes) -> bool
{
  auto* context = mutableContext(context_id);
  const auto transaction = _contexts->transactions.find(context_id);
  if (context == nullptr || transaction == _contexts->transactions.end()) {
    return false;
  }
  std::vector<FastStaBufferMasterChange> mapped;
  for (const auto& change : changes) {
    const auto found = context->buffer_output_node_id_by_inst.find(change.inst_name);
    if (found == context->buffer_output_node_id_by_inst.end()) {
      return false;
    }
    if (context->nodes.at(found->second).cell_master != change.cell_master) {
      mapped.push_back({.node_id = found->second, .cell_master = change.cell_master});
    }
  }
  return mapped.empty() || ContextStore::applyBufferMasters(*context, mapped, true, &transaction->second);
}

auto FastSTA::beginContextTransaction(FastStaContextId context_id) -> FastStaBuildResult
{
  const auto* context = queryContext(context_id);
  if (context == nullptr || !context->timing_valid || context->clock_trial_active || _contexts->transactions.contains(context_id)
      || _contexts->pending_by_committed.contains(context_id)) {
    return {.failure_reason = "complete_context_transaction_unavailable"};
  }
  const auto pending_id = _contexts->contexts.size();
  _contexts->contexts.push_back(nullptr);
  _contexts->context_valid.push_back(true);
  _contexts->transactions.emplace(pending_id, ContextStore::ContextTransaction{.committed_id = context_id, .pending_id = pending_id, .edits = {}});
  _contexts->pending_by_committed.emplace(context_id, pending_id);
  return {.context_id = pending_id, .failure_reason = {}};
}

auto FastSTA::commitContextTransaction(FastStaContextId pending_id) -> bool
{
  const auto found = _contexts->transactions.find(pending_id);
  if (found == _contexts->transactions.end()) {
    return false;
  }
  auto& transaction = found->second;
  auto& context = *_contexts->contexts.at(transaction.committed_id);
  transaction.activate(context, true);
  if (!context.timing_valid || context.clock_trial_active) {
    return false;
  }
  _contexts->contexts.at(pending_id) = std::move(_contexts->contexts.at(transaction.committed_id));
  _contexts->context_valid.at(transaction.committed_id) = false;
  _contexts->pending_by_committed.erase(transaction.committed_id);
  _contexts->transactions.erase(found);
  return true;
}

auto FastSTA::discardContextTransaction(FastStaContextId pending_id) -> bool
{
  const auto found = _contexts->transactions.find(pending_id);
  if (found == _contexts->transactions.end()) {
    return false;
  }
  auto& transaction = found->second;
  transaction.activate(*_contexts->contexts.at(transaction.committed_id), false);
  _contexts->context_valid.at(pending_id) = false;
  _contexts->pending_by_committed.erase(transaction.committed_id);
  _contexts->transactions.erase(found);
  return true;
}

auto FastSTA::collectClockRouteGeometry(const ClockLayout& layout, std::size_t clock_index) -> FastStaClockRouteGeometry
{
  FastStaClockRouteGeometry geometry{.design_dbu_per_um = layout.get_design_dbu_per_um(), .clock_nets = {}};
  const auto* clock = layout.findClock(clock_index);
  if (clock == nullptr) {
    return geometry;
  }
  geometry.clock_nets.reserve(clock->nets.size());
  for (const auto& net : clock->nets) {
    FastStaClockNetRouteGeometry route{.net_name = net.net_name, .routed_segments = {}};
    route.routed_segments.reserve(net.routed_segments.size());
    for (const auto& segment : net.routed_segments) {
      if (segment.routed && !segment.degraded) {
        route.routed_segments.push_back(
            FastStaRcSegment{.begin = {segment.begin.get_x(), segment.begin.get_y()}, .end = {segment.end.get_x(), segment.end.get_y()}});
      }
    }
    if (!route.routed_segments.empty()) {
      geometry.clock_nets.push_back(std::move(route));
    }
  }
  return geometry;
}

auto FastSTA::eraseContext(FastStaContextId context_id) -> bool
{
  if (_contexts->transactions.contains(context_id)) {
    return discardContextTransaction(context_id);
  }
  if (const auto pending = _contexts->pending_by_committed.find(context_id); pending != _contexts->pending_by_committed.end()) {
    (void) discardContextTransaction(pending->second);
  }
  if (context_id >= _contexts->contexts.size() || context_id >= _contexts->context_valid.size() || !_contexts->context_valid.at(context_id)
      || _contexts->contexts.at(context_id) == nullptr) {
    return false;
  }
  _contexts->context_valid.at(context_id) = false;
  _contexts->clock_trials.erase(context_id);
  _contexts->contexts.at(context_id).reset();
  return true;
}

auto FastSTA::reset() -> void
{
  // IDs are monotonic for this authority's lifetime.  Tombstones prevent an
  // old borrowed ID from aliasing a new Design after reset/reinput.
  for (auto& context : _contexts->contexts) {
    context.reset();
  }
  std::fill(_contexts->context_valid.begin(), _contexts->context_valid.end(), false);
  for (auto& context : _contexts->char_contexts) {
    context.reset();
  }
  std::fill(_contexts->char_context_valid.begin(), _contexts->char_context_valid.end(), false);
  _environment = std::nullopt;
  _contexts->clock_trials.clear();
  _contexts->transactions.clear();
  _contexts->pending_by_committed.clear();
}

auto FastSTA::buildCharContext(const FastStaCharTopologySpec& spec) -> FastStaCharBuildResult
{
  auto build = FastStaChar::buildContext(spec);
  if (!build.context.has_value()) {
    return FastStaCharBuildResult{.failure_reason = std::move(build.failure_reason)};
  }
  const auto char_context_id = _contexts->char_contexts.size();
  _contexts->char_contexts.push_back(std::make_unique<FastStaChar::Context>(std::move(build.context).value()));
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
  auto& context = *_contexts->char_contexts.at(char_context_id);
  return FastStaChar::setLoad(context, effective_load_pf);
}

auto FastSTA::runCharSample(FastStaCharContextId char_context_id, double input_slew_ns) -> FastStaCharSampleResult
{
  if (char_context_id >= _contexts->char_contexts.size() || char_context_id >= _contexts->char_context_valid.size()
      || !_contexts->char_context_valid.at(char_context_id) || _contexts->char_contexts.at(char_context_id) == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: characterization sample skipped because char context id is invalid.");
    return {};
  }
  auto& context = *_contexts->char_contexts.at(char_context_id);
  return FastStaChar::runSample(context, input_slew_ns);
}

auto FastSTA::changeBufferMasters(FastStaContextId context_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr || context->clock_trial_active) {
    CTSLOG.warn(Loc::current(), "FastSTA: buffer master batch change skipped because context id is invalid.");
    return false;
  }
  if (changes.empty()) {
    return context->timing_valid && context->power_valid;
  }
  const auto transaction = _contexts->transactions.find(context_id);
  return ContextStore::applyBufferMasters(*context, changes, true, transaction == _contexts->transactions.end() ? nullptr : &transaction->second);
}

auto FastSTA::changeBufferMastersTimingOnly(FastStaContextId context_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr || context->clock_trial_active) {
    CTSLOG.warn(Loc::current(), "FastSTA: timing-only buffer master batch change skipped because context id is invalid.");
    return false;
  }
  if (changes.empty()) {
    return context->timing_valid;
  }
  const auto transaction = _contexts->transactions.find(context_id);
  return ContextStore::applyBufferMasters(*context, changes, false, transaction == _contexts->transactions.end() ? nullptr : &transaction->second);
}

auto FastSTA::beginBufferMastersClockTrial(FastStaContextId context_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: clock trial skipped because context id is invalid.");
    return false;
  }
  if (changes.empty() || context->clock_trial_active || _contexts->transactions.contains(context_id)
      || !(context->timing_valid || context->clock_timing_valid)) {
    return false;
  }
  const auto proposed_region = FastStaIncremental::describeClockBufferMasterRegion(*context, changes);
  if (!proposed_region.has_value()) {
    return false;
  }
  _contexts->clock_trials.try_emplace(context_id, *context, changes, &*proposed_region);
  context->clock_trial_active = true;
  if (!FastStaIncremental::applyPreparedClockBufferMasters(*context, changes) || !FastStaTiming::updateClockTrialRegion(*context, *proposed_region)) {
    _contexts->clock_trials.at(context_id).swap(*context);
    _contexts->clock_trials.erase(context_id);
    return false;
  }
  if (!(_contexts->clock_trials.at(context_id).power_valid ? FastStaPower::updatePreparedRegion(*context, *proposed_region) : FastStaPower::update(*context))) {
    _contexts->clock_trials.at(context_id).swap(*context);
    _contexts->clock_trials.erase(context_id);
    return false;
  }
  return context->clock_timing_valid;
}

auto FastSTA::restoreBufferMastersClockTrial(FastStaContextId context_id) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr || !context->clock_trial_active || !_contexts->clock_trials.contains(context_id)) {
    return false;
  }
  _contexts->clock_trials.at(context_id).swap(*context);
  _contexts->clock_trials.erase(context_id);
  return context->clock_timing_valid;
}

auto FastSTA::commitBufferMastersClockTrial(FastStaContextId context_id) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr || !context->clock_trial_active || !_contexts->clock_trials.contains(context_id)) {
    return false;
  }
  if (!context->clock_timing_valid) {
    auto& snapshot = _contexts->clock_trials.at(context_id);
    snapshot.swap(*context);
    _contexts->clock_trials.erase(context_id);
    return false;
  }
  _contexts->clock_trials.erase(context_id);
  context->clock_trial_active = false;
  context->timing_valid = false;
  context->logic_tags_valid = false;
  context->power_valid = false;
  return context->clock_timing_valid;
}

auto FastSTA::updateTiming(FastStaContextId context_id) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr || context->clock_trial_active || _contexts->transactions.contains(context_id)) {
    CTSLOG.warn(Loc::current(), "FastSTA: timing update skipped because context id is invalid.");
    return false;
  }
  return FastStaTiming::update(*context);
}

auto FastSTA::updatePower(FastStaContextId context_id) -> bool
{
  auto* context = mutableContext(context_id);
  if (context == nullptr || context->clock_trial_active || _contexts->transactions.contains(context_id)) {
    CTSLOG.warn(Loc::current(), "FastSTA: power update skipped because context id is invalid.");
    return false;
  }
  return FastStaPower::update(*context);
}

auto FastSTA::injectNetRouteTree(FastStaContextId context_id, const Net& net, const ClockSteinerTree<int>& route_tree,
                                 FastStaClockNetRcTreeCounts& rc_tree_counts) -> bool
{
  rc_tree_counts = {};
  auto* context = mutableContext(context_id);
  if (_contexts->transactions.contains(context_id) || context == nullptr) {
    CTSLOG.warn(Loc::current(), "FastSTA: route-tree injection skipped because context id is invalid.");
    return false;
  }
  const auto net_iter = context->net_id_by_name.find(net.get_name());
  if (context->clock_trial_active || net_iter == context->net_id_by_name.end() || net_iter->second >= context->nets.size()) {
    return false;
  }
  auto original = context->nets.at(net_iter->second);
  const auto timing_valid = context->timing_valid;
  const auto power_valid = context->power_valid;
  const auto summary = context->timing_summary;
  if (!FastStaBuilder::injectNetRouteTree(*context, net, route_tree)) {
    context->nets.at(net_iter->second) = std::move(original);
    context->timing_valid = timing_valid;
    context->power_valid = power_valid;
    context->timing_summary = summary;
    return false;
  }
  const auto& parasitic = context->nets.at(net_iter->second).parasitic;
  rc_tree_counts.rc_node_count = parasitic.rc_nodes.size();
  rc_tree_counts.rc_edge_count = parasitic.rc_edges.size();
  return true;
}

auto FastSTA::queryContext(FastStaContextId context_id) const -> const FastStaContext*
{
  if (context_id >= _contexts->contexts.size() || context_id >= _contexts->context_valid.size() || !_contexts->context_valid.at(context_id)) {
    return nullptr;
  }
  auto storage_id = context_id;
  if (const auto transaction = _contexts->transactions.find(context_id); transaction != _contexts->transactions.end()) {
    storage_id = transaction->second.committed_id;
    transaction->second.activate(*_contexts->contexts.at(storage_id), true);
  } else if (const auto pending = _contexts->pending_by_committed.find(context_id); pending != _contexts->pending_by_committed.end()) {
    _contexts->transactions.at(pending->second).activate(*_contexts->contexts.at(storage_id), false);
  }
  const auto* context = _contexts->contexts.at(storage_id).get();
  if (context == nullptr) {
    return nullptr;
  }
  if (context->wrapper != nullptr && context->liberty_revision != 0U && context->liberty_revision != context->wrapper->queryLibertyRevision()) {
    return nullptr;
  }
  return context;
}

auto FastSTA::mutableContext(FastStaContextId context_id) -> FastStaContext*
{
  if (_contexts->pending_by_committed.contains(context_id) || queryContext(context_id) == nullptr) {
    return nullptr;
  }
  const auto transaction = _contexts->transactions.find(context_id);
  return _contexts->contexts.at(transaction == _contexts->transactions.end() ? context_id : transaction->second.committed_id).get();
}

}  // namespace icts
