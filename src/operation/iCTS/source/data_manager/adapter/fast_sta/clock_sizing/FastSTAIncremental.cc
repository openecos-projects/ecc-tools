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
 * @file FastSTAIncremental.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Incremental update coordinator implementation for CTS fast STA contexts.
 */

#include "FastSTAIncremental.hh"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTAClockState.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "Logger.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"

namespace icts {
namespace {

auto normalizeBufferInputNodeId(const FastStaContext& context, FastStaNodeId node_id) -> FastStaNodeId
{
  if (node_id >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto& node = context.nodes.at(node_id);
  if (node.kind == FastStaNodeKind::kBufferInput) {
    return node_id;
  }
  if (node.kind != FastStaNodeKind::kBufferOutput || node.inst_name.empty()) {
    return kInvalidFastStaNodeId;
  }
  if (const auto indexed = context.buffer_input_node_id_by_inst.find(node.inst_name); indexed != context.buffer_input_node_id_by_inst.end()) {
    if (indexed->second < context.nodes.size()) {
      const auto& input_node = context.nodes.at(indexed->second);
      if (input_node.kind == FastStaNodeKind::kBufferInput && input_node.inst_name == node.inst_name) {
        return indexed->second;
      }
    }
  }
  return kInvalidFastStaNodeId;
}

auto normalizeBufferOutputNodeId(const FastStaContext& context, FastStaNodeId node_id) -> FastStaNodeId
{
  if (node_id >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto& node = context.nodes.at(node_id);
  if (node.kind == FastStaNodeKind::kBufferOutput) {
    return node_id;
  }
  if (node.kind != FastStaNodeKind::kBufferInput || node.inst_name.empty()) {
    return kInvalidFastStaNodeId;
  }
  if (const auto indexed = context.buffer_output_node_id_by_inst.find(node.inst_name); indexed != context.buffer_output_node_id_by_inst.end()) {
    if (indexed->second < context.nodes.size()) {
      const auto& output_node = context.nodes.at(indexed->second);
      if (output_node.kind == FastStaNodeKind::kBufferOutput && output_node.inst_name == node.inst_name) {
        return indexed->second;
      }
    }
  }
  return kInvalidFastStaNodeId;
}

auto isClockTrialNode(const FastStaContext& context, FastStaNodeId node_id) -> bool
{
  if (node_id >= context.nodes.size() || context.nodes.at(node_id).domain != FastStaNodeDomain::kClock) {
    return false;
  }
  if (!context.owner_clock_scope_available) {
    return true;
  }
  return std::ranges::find(context.owned_clock_node_ids, node_id) != context.owned_clock_node_ids.end();
}

auto markReachableFromNode(const FastStaContext& context, FastStaNodeId node_id, FastStaDirtyRegion& dirty_region, std::unordered_set<FastStaNodeId>& node_seen,
                           std::unordered_set<FastStaNetId>& net_seen, bool clock_only) -> void
{
  std::vector<FastStaNodeId> pending_nodes{node_id};
  while (!pending_nodes.empty()) {
    const auto current_node_id = pending_nodes.back();
    pending_nodes.pop_back();
    if (current_node_id >= context.nodes.size() || node_seen.contains(current_node_id)) {
      continue;
    }

    node_seen.insert(current_node_id);
    if (clock_only && context.nodes.at(current_node_id).domain != FastStaNodeDomain::kClock) {
      continue;
    }
    dirty_region.node_ids.push_back(current_node_id);
    const auto& node = context.nodes.at(current_node_id);
    if (node.kind == FastStaNodeKind::kBufferInput) {
      const auto output_id = normalizeBufferOutputNodeId(context, current_node_id);
      if (output_id != kInvalidFastStaNodeId) {
        pending_nodes.push_back(output_id);
      }
      continue;
    }

    for (const auto net_id : node.output_net_ids) {
      if (net_id >= context.nets.size()) {
        continue;
      }
      if (clock_only && context.nets.at(net_id).domain != FastStaNetDomain::kClock) {
        continue;
      }
      if (!net_seen.contains(net_id)) {
        net_seen.insert(net_id);
        dirty_region.net_ids.push_back(net_id);
      }
      for (const auto load_node_id : context.nets.at(net_id).load_node_ids) {
        pending_nodes.push_back(load_node_id);
      }
    }
  }
}

auto dirtyRegionStartNode(const FastStaContext& context, FastStaNodeId changed_input_node_id) -> FastStaNodeId
{
  if (changed_input_node_id >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }

  auto start_node_id = changed_input_node_id;
  const auto incoming_net_id = context.nodes.at(changed_input_node_id).incoming_net_id;
  if (incoming_net_id < context.nets.size()) {
    const auto incoming_driver_id = context.nets.at(incoming_net_id).driver_node_id;
    if (incoming_driver_id < context.nodes.size() && context.nodes.at(incoming_driver_id).kind == FastStaNodeKind::kBufferOutput) {
      const auto driver_input_id = normalizeBufferInputNodeId(context, incoming_driver_id);
      if (driver_input_id != kInvalidFastStaNodeId) {
        start_node_id = driver_input_id;
      }
    } else if (incoming_driver_id < context.nodes.size()) {
      start_node_id = incoming_driver_id;
    }
  }
  return start_node_id;
}

auto parentNodeId(const FastStaContext& context, FastStaNodeId node_id) -> FastStaNodeId
{
  if (node_id >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  const auto& node = context.nodes.at(node_id);
  if (node.kind == FastStaNodeKind::kBufferOutput) {
    return normalizeBufferInputNodeId(context, node_id);
  }
  if (node.incoming_net_id < context.nets.size()) {
    const auto parent_id = context.nets.at(node.incoming_net_id).driver_node_id;
    return parent_id < context.nodes.size() ? parent_id : kInvalidFastStaNodeId;
  }
  return kInvalidFastStaNodeId;
}

auto lowestCommonAncestor(const FastStaContext& context, FastStaNodeId lhs, FastStaNodeId rhs) -> FastStaNodeId
{
  if (lhs >= context.nodes.size() || rhs >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  std::unordered_set<FastStaNodeId> lhs_ancestors;
  auto current = lhs;
  for (std::size_t step = 0U; current < context.nodes.size() && step <= context.nodes.size(); ++step) {
    if (!lhs_ancestors.insert(current).second) {
      return kInvalidFastStaNodeId;
    }
    current = parentNodeId(context, current);
  }

  std::unordered_set<FastStaNodeId> rhs_seen;
  current = rhs;
  for (std::size_t step = 0U; current < context.nodes.size() && step <= context.nodes.size(); ++step) {
    if (lhs_ancestors.contains(current)) {
      return current;
    }
    if (!rhs_seen.insert(current).second) {
      return kInvalidFastStaNodeId;
    }
    current = parentNodeId(context, current);
  }
  return kInvalidFastStaNodeId;
}

auto collectDirtyRegionFromStart(const FastStaContext& context, FastStaNodeId start_node_id, bool clock_only = false) -> FastStaDirtyRegion
{
  FastStaDirtyRegion dirty_region;
  if (start_node_id >= context.nodes.size()) {
    return dirty_region;
  }
  if (context.nodes.at(start_node_id).kind == FastStaNodeKind::kBufferOutput) {
    start_node_id = normalizeBufferInputNodeId(context, start_node_id);
    if (start_node_id == kInvalidFastStaNodeId) {
      return dirty_region;
    }
  }
  if (clock_only && context.nodes.at(start_node_id).domain != FastStaNodeDomain::kClock) {
    return dirty_region;
  }

  dirty_region.valid = true;
  dirty_region.start_node_id = start_node_id;
  std::unordered_set<FastStaNodeId> node_seen;
  std::unordered_set<FastStaNetId> net_seen;
  markReachableFromNode(context, start_node_id, dirty_region, node_seen, net_seen, clock_only);
  return dirty_region;
}

auto collectDirtyRegion(const FastStaContext& context, FastStaNodeId changed_input_node_id) -> FastStaDirtyRegion
{
  return collectDirtyRegionFromStart(context, dirtyRegionStartNode(context, changed_input_node_id));
}

auto prepareBufferMasterChanges(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool;

auto applyBufferMasterChange(FastStaContext& context, FastStaNodeId node_id, std::string_view cell_master, bool invalidate_context) -> FastStaNodeId
{
  if (node_id >= context.nodes.size()) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because node id is invalid.");
    return kInvalidFastStaNodeId;
  }
  auto& node = context.nodes.at(node_id);
  if (node.kind != FastStaNodeKind::kBufferInput && node.kind != FastStaNodeKind::kBufferOutput) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: node \"", node.name, "\" is not a buffer node.");
    return kInvalidFastStaNodeId;
  }
  const auto input_node_id = normalizeBufferInputNodeId(context, node_id);
  if (input_node_id == kInvalidFastStaNodeId) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because buffer input node is unavailable for \"", node.name, "\".");
    return kInvalidFastStaNodeId;
  }
  const auto target_master = std::string(cell_master);
  if (!context.liberty_cell_by_master.contains(target_master)) {
    if (context.wrapper == nullptr) {
      CTSLOG.error(Loc::current(), "FastStaIncremental: Wrapper is unavailable.");
    }
    const auto liberty_cell = FastStaLiberty::extractBufferCell(*context.wrapper, target_master);
    if (!liberty_cell.has_value()) {
      CTSLOG.warn(Loc::current(), "FastStaIncremental: required Liberty data is unavailable for target master \"", target_master, "\".");
      return kInvalidFastStaNodeId;
    }
    context.liberty_cell_by_master.emplace(target_master, *liberty_cell);
  }
  const auto output_node_id = normalizeBufferOutputNodeId(context, input_node_id);
  if (output_node_id == kInvalidFastStaNodeId) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because buffer output node is unavailable for \"", node.name, "\".");
    return kInvalidFastStaNodeId;
  }
  auto& input_node = context.nodes.at(input_node_id);
  auto& output_node = context.nodes.at(output_node_id);
  input_node.cell_master = target_master;
  input_node.input_cap_pf = context.liberty_cell_by_master.at(target_master).input_cap_pf;
  input_node.input_cap_pf_by_timing = context.liberty_cell_by_master.at(target_master).input_cap_pf_by_timing;
  input_node.input_cap_profile_available = context.liberty_cell_by_master.at(target_master).input_cap_profile_available;
  if (input_node.slew_limit_from_master) {
    input_node.max_slew_ns = context.liberty_cell_by_master.at(target_master).input_slew_limit_ns;
  }
  output_node.cell_master = target_master;
  for (auto& arc : context.timing_arcs) {
    if (arc.from_node_id == input_node_id && arc.to_node_id == output_node_id) {
      arc.cell_master = target_master;
    }
  }
  if (output_node.slew_limit_from_master) {
    output_node.max_slew_ns = context.liberty_cell_by_master.at(target_master).output_slew_limit_ns;
  }
  for (const auto net_id : output_node.output_net_ids) {
    if (net_id < context.nets.size() && context.nets.at(net_id).cap_limit_from_master) {
      context.nets.at(net_id).max_cap_pf = context.liberty_cell_by_master.at(target_master).output_cap_limit_pf;
    }
  }
  if (invalidate_context) {
    context.timing_valid = false;
    context.power_valid = false;
  }
  return input_node_id;
}

auto validateBufferMasterChange(const FastStaContext& context, const FastStaBufferMasterChange& change) -> bool
{
  if (change.node_id >= context.nodes.size()) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because node id is invalid.");
    return false;
  }
  const auto& node = context.nodes.at(change.node_id);
  if (node.kind != FastStaNodeKind::kBufferInput && node.kind != FastStaNodeKind::kBufferOutput) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: node \"", node.name, "\" is not a buffer node.");
    return false;
  }
  if (normalizeBufferInputNodeId(context, change.node_id) == kInvalidFastStaNodeId) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because buffer input node is unavailable for \"", node.name, "\".");
    return false;
  }
  if (normalizeBufferOutputNodeId(context, change.node_id) == kInvalidFastStaNodeId) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because buffer output node is unavailable for \"", node.name, "\".");
    return false;
  }
  if (change.cell_master.empty()) {
    CTSLOG.warn(Loc::current(), "FastStaIncremental: buffer master change skipped because target master is empty for \"", node.name, "\".");
    return false;
  }
  return true;
}

auto prepareBufferMasterChanges(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!FastStaIncremental::validateBufferMasterChanges(context, changes)) {
    return false;
  }
  std::vector<std::pair<std::string, FastStaLibertyCell>> missing_cells;
  for (const auto& change : changes) {
    if (context.liberty_cell_by_master.contains(change.cell_master)) {
      continue;
    }
    if (context.wrapper == nullptr) {
      CTSLOG.error(Loc::current(), "FastStaIncremental: Wrapper is unavailable.");
    }
    if (std::ranges::any_of(missing_cells, [&](const auto& cell) -> bool { return cell.first == change.cell_master; })) {
      continue;
    }
    const auto liberty_cell = FastStaLiberty::extractBufferCell(*context.wrapper, change.cell_master);
    if (!liberty_cell.has_value()) {
      CTSLOG.warn(Loc::current(), "FastStaIncremental: required Liberty data is unavailable for target master \"", change.cell_master, "\".");
      return false;
    }
    missing_cells.emplace_back(change.cell_master, *liberty_cell);
  }
  for (auto& [cell_master, liberty_cell] : missing_cells) {
    context.liberty_cell_by_master.emplace(std::move(cell_master), std::move(liberty_cell));
  }
  for (const auto& change : changes) {
    const auto& current = context.nodes.at(change.node_id);
    const auto source = context.liberty_cell_by_master.find(current.cell_master);
    const auto& target = context.liberty_cell_by_master.at(change.cell_master);
    if (source == context.liberty_cell_by_master.end() || source->second.input_port != target.input_port || source->second.output_port != target.output_port
        || source->second.timing_arc.positive_unate != target.timing_arc.positive_unate
        || source->second.timing_arc.negative_unate != target.timing_arc.negative_unate) {
      CTSLOG.warn(Loc::current(), "FastStaIncremental: master change has incompatible ports or timing sense for \"", current.name, "\".");
      return false;
    }
  }
  return true;
}

}  // namespace

auto FastStaIncremental::changeBufferMaster(FastStaContext& context, FastStaNodeId node_id, std::string_view cell_master) -> bool
{
  return applyBufferMasterChange(context, node_id, cell_master, true) != kInvalidFastStaNodeId;
}

auto FastStaIncremental::validateBufferMasterChanges(const FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  for (const auto& change : changes) {
    if (!validateBufferMasterChange(context, change)) {
      return false;
    }
  }
  return true;
}

auto FastStaIncremental::changeBufferMasters(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!prepareBufferMasterChanges(context, changes)) {
    return false;
  }
  for (const auto& change : changes) {
    if (applyBufferMasterChange(context, change.node_id, change.cell_master, false) == kInvalidFastStaNodeId) {
      context.timing_valid = false;
      context.power_valid = false;
      return false;
    }
  }
  context.timing_valid = false;
  context.power_valid = false;
  return true;
}

auto FastStaIncremental::describeBufferMasterRegion(const FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
    -> std::optional<FastStaDirtyRegion>
{
  if (changes.empty() || !validateBufferMasterChanges(context, changes)) {
    return std::nullopt;
  }
  std::vector<FastStaNodeId> starts;
  for (const auto& change : changes) {
    const auto input_node_id = normalizeBufferInputNodeId(context, change.node_id);
    const auto start_node_id = dirtyRegionStartNode(context, input_node_id);
    if (start_node_id == kInvalidFastStaNodeId) {
      return std::nullopt;
    }
    bool merged = false;
    for (auto& start : starts) {
      const auto ancestor = lowestCommonAncestor(context, start, start_node_id);
      if (ancestor != kInvalidFastStaNodeId) {
        start = ancestor;
        merged = true;
        break;
      }
    }
    if (!merged) {
      starts.push_back(start_node_id);
    }
  }
  if (starts.size() == 1U) {
    return collectDirtyRegionFromStart(context, starts.front());
  }
  FastStaDirtyRegion dirty_region{
      .valid = true, .start_node_id = starts.front(), .node_ids = {}, .net_ids = {}, .load_update_net_ids = {}, .start_node_ids = starts};
  std::unordered_set<FastStaNodeId> node_seen;
  std::unordered_set<FastStaNetId> net_seen;
  for (const auto start : starts) {
    markReachableFromNode(context, start, dirty_region, node_seen, net_seen, false);
  }
  return dirty_region;
}

auto FastStaIncremental::describeClockBufferMasterRegion(const FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
    -> std::optional<FastStaDirtyRegion>
{
  if (changes.empty() || !validateBufferMasterChanges(context, changes)) {
    return std::nullopt;
  }
  std::vector<FastStaNodeId> starts;
  starts.reserve(changes.size());
  for (const auto& change : changes) {
    const auto input_node_id = normalizeBufferInputNodeId(context, change.node_id);
    const auto output_node_id = normalizeBufferOutputNodeId(context, change.node_id);
    if (input_node_id == kInvalidFastStaNodeId || output_node_id == kInvalidFastStaNodeId || input_node_id >= context.nodes.size()
        || output_node_id >= context.nodes.size() || !isClockTrialNode(context, input_node_id) || !isClockTrialNode(context, output_node_id)) {
      return std::nullopt;
    }
    const auto start_node_id = dirtyRegionStartNode(context, input_node_id);
    if (start_node_id == kInvalidFastStaNodeId || start_node_id >= context.nodes.size()
        || context.nodes.at(start_node_id).domain != FastStaNodeDomain::kClock) {
      return std::nullopt;
    }
    bool merged = false;
    for (auto& start : starts) {
      const auto ancestor = lowestCommonAncestor(context, start, start_node_id);
      if (ancestor != kInvalidFastStaNodeId) {
        if (context.nodes.at(ancestor).domain != FastStaNodeDomain::kClock) {
          return std::nullopt;
        }
        start = ancestor;
        merged = true;
        break;
      }
    }
    if (!merged) {
      starts.push_back(start_node_id);
    }
  }
  if (starts.empty()) {
    return std::nullopt;
  }
  auto dirty_region = FastStaDirtyRegion{};
  if (starts.size() == 1U) {
    dirty_region = collectDirtyRegionFromStart(context, starts.front(), true);
  } else {
    dirty_region = {.valid = true, .start_node_id = starts.front(), .node_ids = {}, .net_ids = {}, .load_update_net_ids = {}, .start_node_ids = starts};
    std::unordered_set<FastStaNodeId> node_seen;
    std::unordered_set<FastStaNetId> net_seen;
    for (const auto start : starts) {
      markReachableFromNode(context, start, dirty_region, node_seen, net_seen, true);
    }
  }
  if (!dirty_region.valid || dirty_region.node_ids.empty()) {
    return std::nullopt;
  }
  for (const auto& change : changes) {
    const auto input_node_id = normalizeBufferInputNodeId(context, change.node_id);
    const auto incoming_net_id = context.nodes.at(input_node_id).incoming_net_id;
    if (incoming_net_id < context.nets.size()
        && std::ranges::find(dirty_region.load_update_net_ids, incoming_net_id) == dirty_region.load_update_net_ids.end()) {
      dirty_region.load_update_net_ids.push_back(incoming_net_id);
    }
  }
  return dirty_region;
}

auto FastStaIncremental::changeBufferMastersIncremental(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
    -> std::optional<FastStaDirtyRegion>
{
  auto dirty_region = describeBufferMasterRegion(context, changes);
  if (!dirty_region.has_value() || !prepareBufferMasterChanges(context, changes)) {
    return std::nullopt;
  }
  for (const auto& change : changes) {
    const auto input_node_id = normalizeBufferInputNodeId(context, change.node_id);
    if (input_node_id < context.nodes.size()) {
      const auto incoming_net_id = context.nodes.at(input_node_id).incoming_net_id;
      if (incoming_net_id < context.nets.size()
          && std::ranges::find(dirty_region->load_update_net_ids, incoming_net_id) == dirty_region->load_update_net_ids.end()) {
        dirty_region->load_update_net_ids.push_back(incoming_net_id);
      }
    }
  }
  for (const auto& change : changes) {
    if (applyBufferMasterChange(context, change.node_id, change.cell_master, false) == kInvalidFastStaNodeId) {
      context.timing_valid = false;
      context.power_valid = false;
      return std::nullopt;
    }
  }
  return dirty_region;
}

auto FastStaIncremental::changeBufferMastersClockIncremental(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
    -> std::optional<FastStaDirtyRegion>
{
  auto dirty_region = describeClockBufferMasterRegion(context, changes);
  if (!dirty_region.has_value() || !applyPreparedClockBufferMasters(context, changes)) {
    return std::nullopt;
  }
  return dirty_region;
}

auto FastStaIncremental::applyPreparedClockBufferMasters(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!prepareBufferMasterChanges(context, changes)) {
    return false;
  }
  for (const auto& change : changes) {
    if (applyBufferMasterChange(context, change.node_id, change.cell_master, false) == kInvalidFastStaNodeId) {
      context.timing_valid = false;
      context.clock_timing_valid = false;
      context.power_valid = false;
      return false;
    }
  }
  return true;
}

auto FastStaIncremental::changeBufferMasterIncremental(FastStaContext& context, FastStaNodeId node_id, std::string_view cell_master)
    -> std::optional<FastStaDirtyRegion>
{
  const auto input_node_id = applyBufferMasterChange(context, node_id, cell_master, false);
  if (input_node_id == kInvalidFastStaNodeId) {
    return std::nullopt;
  }
  return collectDirtyRegion(context, input_node_id);
}

}  // namespace icts
