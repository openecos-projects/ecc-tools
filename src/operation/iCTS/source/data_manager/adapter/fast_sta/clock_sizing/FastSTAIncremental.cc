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
#include <utility>
#include <vector>

#include "FastSTAClockState.hh"
#include "FastSTALiberty.hh"
#include "FastSTALibertyModel.hh"
#include "Logger.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"

namespace icts {
namespace {

auto normalizeBufferInputNodeId(const FastStaClockContext& context, FastStaNodeId node_id) -> FastStaNodeId
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

auto normalizeBufferOutputNodeId(const FastStaClockContext& context, FastStaNodeId node_id) -> FastStaNodeId
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

auto markReachableFromNode(const FastStaClockContext& context, FastStaNodeId node_id, FastStaDirtyRegion& dirty_region, std::vector<std::uint8_t>& node_seen,
                           std::vector<std::uint8_t>& net_seen) -> void
{
  std::vector<FastStaNodeId> pending_nodes{node_id};
  while (!pending_nodes.empty()) {
    const auto current_node_id = pending_nodes.back();
    pending_nodes.pop_back();
    if (current_node_id >= context.nodes.size() || node_seen.at(current_node_id) != 0U) {
      continue;
    }

    node_seen.at(current_node_id) = 1U;
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
      if (net_seen.at(net_id) == 0U) {
        net_seen.at(net_id) = 1U;
        dirty_region.net_ids.push_back(net_id);
      }
      for (const auto load_node_id : context.nets.at(net_id).load_node_ids) {
        pending_nodes.push_back(load_node_id);
      }
    }
  }
}

auto dirtyRegionStartNode(const FastStaClockContext& context, FastStaNodeId changed_input_node_id) -> FastStaNodeId
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

auto parentNodeId(const FastStaClockContext& context, FastStaNodeId node_id) -> FastStaNodeId
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

auto lowestCommonAncestor(const FastStaClockContext& context, FastStaNodeId lhs, FastStaNodeId rhs) -> FastStaNodeId
{
  if (lhs >= context.nodes.size() || rhs >= context.nodes.size()) {
    return kInvalidFastStaNodeId;
  }
  std::vector<std::uint8_t> lhs_ancestors(context.nodes.size(), 0U);
  auto current = lhs;
  for (std::size_t step = 0U; current < context.nodes.size() && step <= context.nodes.size(); ++step) {
    if (lhs_ancestors.at(current) != 0U) {
      return kInvalidFastStaNodeId;
    }
    lhs_ancestors.at(current) = 1U;
    current = parentNodeId(context, current);
  }

  std::vector<std::uint8_t> rhs_seen(context.nodes.size(), 0U);
  current = rhs;
  for (std::size_t step = 0U; current < context.nodes.size() && step <= context.nodes.size(); ++step) {
    if (lhs_ancestors.at(current) != 0U) {
      return current;
    }
    if (rhs_seen.at(current) != 0U) {
      return kInvalidFastStaNodeId;
    }
    rhs_seen.at(current) = 1U;
    current = parentNodeId(context, current);
  }
  return kInvalidFastStaNodeId;
}

auto collectDirtyRegionFromStart(const FastStaClockContext& context, FastStaNodeId start_node_id) -> FastStaDirtyRegion
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

  dirty_region.valid = true;
  dirty_region.start_node_id = start_node_id;
  std::vector<std::uint8_t> node_seen(context.nodes.size(), 0U);
  std::vector<std::uint8_t> net_seen(context.nets.size(), 0U);
  markReachableFromNode(context, start_node_id, dirty_region, node_seen, net_seen);
  return dirty_region;
}

auto collectDirtyRegion(const FastStaClockContext& context, FastStaNodeId changed_input_node_id) -> FastStaDirtyRegion
{
  return collectDirtyRegionFromStart(context, dirtyRegionStartNode(context, changed_input_node_id));
}

auto prepareBufferMasterChanges(FastStaClockContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool;

auto applyBufferMasterChange(FastStaClockContext& context, FastStaNodeId node_id, std::string_view cell_master, bool invalidate_context) -> FastStaNodeId
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
  input_node.max_slew_ns = context.liberty_cell_by_master.at(target_master).input_slew_limit_ns;
  output_node.cell_master = target_master;
  if (invalidate_context) {
    context.timing_valid = false;
    context.power_valid = false;
  }
  return input_node_id;
}

auto validateBufferMasterChange(const FastStaClockContext& context, const FastStaBufferMasterChange& change) -> bool
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

auto prepareBufferMasterChanges(FastStaClockContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
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
  return true;
}

}  // namespace

auto FastStaIncremental::changeBufferMaster(FastStaClockContext& context, FastStaNodeId node_id, std::string_view cell_master) -> bool
{
  return applyBufferMasterChange(context, node_id, cell_master, true) != kInvalidFastStaNodeId;
}

auto FastStaIncremental::validateBufferMasterChanges(const FastStaClockContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  for (const auto& change : changes) {
    if (!validateBufferMasterChange(context, change)) {
      return false;
    }
  }
  return true;
}

auto FastStaIncremental::changeBufferMasters(FastStaClockContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool
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

auto FastStaIncremental::changeBufferMastersIncremental(FastStaClockContext& context, const std::vector<FastStaBufferMasterChange>& changes)
    -> std::optional<FastStaDirtyRegion>
{
  if (changes.empty() || !prepareBufferMasterChanges(context, changes)) {
    return std::nullopt;
  }
  auto common_start_node_id = kInvalidFastStaNodeId;
  for (const auto& change : changes) {
    const auto input_node_id = normalizeBufferInputNodeId(context, change.node_id);
    const auto start_node_id = dirtyRegionStartNode(context, input_node_id);
    if (start_node_id == kInvalidFastStaNodeId) {
      return std::nullopt;
    }
    common_start_node_id = common_start_node_id == kInvalidFastStaNodeId ? start_node_id : lowestCommonAncestor(context, common_start_node_id, start_node_id);
    if (common_start_node_id == kInvalidFastStaNodeId) {
      return std::nullopt;
    }
  }
  auto dirty_region = collectDirtyRegionFromStart(context, common_start_node_id);
  if (!dirty_region.valid) {
    return std::nullopt;
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

auto FastStaIncremental::changeBufferMasterIncremental(FastStaClockContext& context, FastStaNodeId node_id, std::string_view cell_master)
    -> std::optional<FastStaDirtyRegion>
{
  const auto input_node_id = applyBufferMasterChange(context, node_id, cell_master, false);
  if (input_node_id == kInvalidFastStaNodeId) {
    return std::nullopt;
  }
  return collectDirtyRegion(context, input_node_id);
}

}  // namespace icts
