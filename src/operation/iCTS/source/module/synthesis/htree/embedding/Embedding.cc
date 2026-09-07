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
 * @file Embedding.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Builds CTS inst, pin, and net objects for a selected H-tree topology.
 */

#include "synthesis/htree/embedding/Embedding.hh"

#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "Design.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Logger.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "Tree.hh"
#include "io/Wrapper.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "synthesis/htree/embedding/BufferPortTable.hh"
#include "synthesis/htree/embedding/EmbeddingState.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts::htree {

auto ClassifySplitPlanMaterialization(const SinkLoadRegionSplitPlan& plan, const std::vector<Pin*>& terminal_loads, const Point<int>& upstream_anchor)
    -> SplitPlanMaterializationDecision
{
  if (!plan.feasible) {
    return SplitPlanMaterializationDecision::kMismatch;
  }
  if (!plan.required) {
    return SplitPlanMaterializationDecision::kPassThrough;
  }

  const bool anchor_matches = plan.anchor.get_x() == upstream_anchor.get_x() && plan.anchor.get_y() == upstream_anchor.get_y();
  const std::unordered_set<const Pin*> planned_loads(plan.original_loads.begin(), plan.original_loads.end());
  const bool loads_match = planned_loads.size() == terminal_loads.size()
                           && std::ranges::all_of(terminal_loads, [&planned_loads](const Pin* load) -> bool { return planned_loads.contains(load); });
  return anchor_matches && loads_match ? SplitPlanMaterializationDecision::kMaterialize : SplitPlanMaterializationDecision::kMismatch;
}

namespace {

auto CreateBufferInstance(HTree::Build& result, const std::string& inst_name, const std::string& cell_master, const Point<int>& location,
                          const std::string& input_pin_name, const std::string& output_pin_name) -> std::pair<Pin*, Pin*>
{
  auto inst = std::make_unique<Inst>(inst_name, cell_master, InstType::kBuffer, location);
  auto* inst_ptr = inst.get();

  auto input_pin = std::make_unique<Pin>(input_pin_name, PinType::kIn, location, inst_ptr, nullptr, false);
  auto* input_pin_ptr = input_pin.get();
  result.output.inserted_pins.push_back(std::move(input_pin));

  auto output_pin = std::make_unique<Pin>(output_pin_name, PinType::kOut, location, inst_ptr, nullptr, false);
  auto* output_pin_ptr = output_pin.get();
  result.output.inserted_pins.push_back(std::move(output_pin));

  inst_ptr->add_pin(input_pin_ptr);
  inst_ptr->add_pin(output_pin_ptr);

  result.output.inserted_insts.push_back(std::move(inst));
  result.output.propagation_arcs.push_back(ClockPropagationArc{
      .inst = inst_ptr,
      .input_pin = input_pin_ptr,
      .output_pin = output_pin_ptr,
      .kind = ClockPropagationKind::kBuffer,
      .origin = ClockPropagationOrigin::kSynthesized,
      .path_buffer_weight = 1,
  });

  return {input_pin_ptr, output_pin_ptr};
}

auto RecordInsertedInstLevel(HTree::Build& result, Inst* inst, int topology_level, std::size_t index_in_level) -> void
{
  if (inst == nullptr) {
    return;
  }
  result.output.inserted_inst_levels.push_back(HTree::InsertedInstLevel{
      .inst = inst,
      .topology_level = topology_level,
      .index_in_level = index_in_level,
  });
}

auto RecordInsertedNetLevel(HTree::Build& result, Net* net, int topology_level) -> void
{
  if (net == nullptr) {
    return;
  }
  const auto index_in_level = static_cast<std::size_t>(std::ranges::count_if(
      result.output.inserted_net_levels, [topology_level](const HTree::InsertedNetLevel& level) -> bool { return level.topology_level == topology_level; }));
  result.output.inserted_net_levels.push_back(HTree::InsertedNetLevel{
      .net = net,
      .topology_level = topology_level,
      .index_in_level = index_in_level,
  });
}

auto EraseInsertedInstLevel(HTree::Build& result, const Inst* inst) -> void
{
  if (inst == nullptr) {
    return;
  }
  std::erase_if(result.output.inserted_inst_levels, [inst](const HTree::InsertedInstLevel& level) -> bool { return level.inst == inst; });
}

auto EraseInsertedNetLevel(HTree::Build& result, const Net* net) -> void
{
  if (net == nullptr) {
    return;
  }
  std::erase_if(result.output.inserted_net_levels, [net](const HTree::InsertedNetLevel& level) -> bool { return level.net == net; });
}

auto ConnectNet(Net* net, Pin* driver, const std::vector<Pin*>& loads) -> void
{
  if (net == nullptr) {
    return;
  }

  net->set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(net);
  }

  net->set_loads({});
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net->add_load(load);
    load->set_net(net);
  }
}

auto CreateNet(HTree::Build& result, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads, int topology_level) -> Net*
{
  auto net = std::make_unique<Net>(net_name);
  auto* net_ptr = net.get();
  ConnectNet(net_ptr, driver, loads);
  RecordInsertedNetLevel(result, net_ptr, topology_level);

  result.output.inserted_nets.push_back(std::move(net));
  return net_ptr;
}

template <typename T>
auto EraseOwnedPointer(std::vector<std::unique_ptr<T>>& objects, T* target) -> void
{
  objects.erase(std::remove_if(objects.begin(), objects.end(), [target](const std::unique_ptr<T>& object) -> bool { return object.get() == target; }),
                objects.end());
}

template <typename T>
auto CollectBorrowedPointers(const std::vector<std::unique_ptr<T>>& objects) -> std::vector<T*>
{
  std::vector<T*> borrowed;
  borrowed.reserve(objects.size());
  for (const auto& object : objects) {
    if (object != nullptr) {
      borrowed.push_back(object.get());
    }
  }
  return borrowed;
}

auto FindUniqueTypedPin(Inst* inst, bool output) -> Pin*
{
  if (inst == nullptr) {
    return nullptr;
  }
  Pin* result = nullptr;
  for (auto* pin : inst->get_pins()) {
    if (pin == nullptr) {
      continue;
    }
    const bool matches = output ? pin->get_type() == PinType::kOut : (pin->get_type() == PinType::kIn || pin->get_type() == PinType::kClock);
    if (!matches) {
      continue;
    }
    if (result != nullptr) {
      return nullptr;
    }
    result = pin;
  }
  return result;
}

auto IsDesignOwnedPin(Design& design, const Pin* pin) -> bool
{
  const auto design_pins = design.get_pins();
  return std::ranges::any_of(design_pins, [pin](const Pin* design_pin) -> bool { return design_pin == pin; });
}

auto MakePinFullNameWithLocalName(const Pin* pin, const std::string& local_name) -> std::string
{
  if (pin == nullptr) {
    return {};
  }

  const auto* inst = pin->get_inst();
  return inst == nullptr ? local_name : inst->get_name() + "/" + local_name;
}

auto CanRenameRootDriverPin(Design& design, Pin* pin, const std::string& local_name) -> bool
{
  if (pin == nullptr || local_name.empty()) {
    return false;
  }
  if (pin->get_name() == local_name) {
    return true;
  }
  if (!IsDesignOwnedPin(design, pin)) {
    return true;
  }

  auto* existing_pin = design.findPin(MakePinFullNameWithLocalName(pin, local_name));
  return existing_pin == nullptr || existing_pin == pin;
}

auto RenameRootDriverPin(Design& design, Pin* pin, const std::string& local_name) -> bool
{
  if (pin == nullptr || local_name.empty()) {
    return false;
  }
  if (pin->get_name() == local_name) {
    return true;
  }
  if (IsDesignOwnedPin(design, pin)) {
    return design.renamePin(pin, local_name);
  }

  pin->set_name(local_name);
  return true;
}

auto ResolveBufferPorts(Wrapper& wrapper, const std::string& cell_master) -> std::optional<std::pair<std::string, std::string>>
{
  if (cell_master.empty()) {
    return std::nullopt;
  }

  const auto ports = wrapper.queryBufferPorts(cell_master);
  if (!ports.has_value()) {
    return std::nullopt;
  }
  return std::make_pair(ports->input, ports->output);
}

auto ReplaceNetLoad(Net* net, Pin* old_load, Pin* new_load) -> bool
{
  if (net == nullptr || old_load == nullptr || new_load == nullptr) {
    return false;
  }

  auto updated_loads = net->get_loads();
  const auto load_it = std::ranges::find(updated_loads, old_load);
  if (load_it == updated_loads.end()) {
    return false;
  }

  *load_it = new_load;
  net->set_loads(updated_loads);
  return true;
}

auto PruneLeafSingleLoadBuffers(HTree::Build& result, const std::unordered_set<Inst*>& protected_insts) -> std::size_t
{
  const auto candidate_insts = CollectBorrowedPointers(result.output.inserted_insts);
  const std::unordered_set<Inst*> inserted_inst_set(candidate_insts.begin(), candidate_insts.end());
  std::size_t pruned_count = 0U;

  for (auto* inst : candidate_insts) {
    if (inst == nullptr || !inst->is_buffer() || protected_insts.contains(inst)) {
      continue;
    }

    auto* output_pin = FindUniqueTypedPin(inst, true);
    if (output_pin == nullptr) {
      continue;
    }
    auto* output_net = output_pin->get_net();
    if (output_net == nullptr || output_net->get_loads().size() != 1U || output_net->get_loads().front() == nullptr) {
      continue;
    }

    auto* downstream_load = output_net->get_loads().front();
    auto* downstream_inst = downstream_load->get_inst();
    if (downstream_inst != nullptr && inserted_inst_set.contains(downstream_inst)) {
      continue;
    }

    auto* input_pin = FindUniqueTypedPin(inst, false);
    if (input_pin == nullptr) {
      continue;
    }
    auto* upstream_net = input_pin->get_net();
    if (upstream_net == nullptr || !ReplaceNetLoad(upstream_net, input_pin, downstream_load)) {
      continue;
    }

    downstream_load->set_net(upstream_net);
    input_pin->set_net(nullptr);
    output_pin->set_net(nullptr);
    output_net->set_driver(nullptr);
    output_net->set_loads({});

    const auto inst_pins = inst->get_pins();
    for (auto* pin : inst_pins) {
      if (pin == nullptr) {
        continue;
      }
      pin->set_inst(nullptr);
      pin->set_net(nullptr);
      EraseOwnedPointer(result.output.inserted_pins, pin);
    }

    EraseInsertedNetLevel(result, output_net);
    EraseOwnedPointer(result.output.inserted_nets, output_net);
    EraseInsertedInstLevel(result, inst);
    std::erase_if(result.output.propagation_arcs, [inst](const ClockPropagationArc& arc) -> bool { return arc.inst == inst; });
    EraseOwnedPointer(result.output.inserted_insts, inst);
    ++pruned_count;
  }

  return pruned_count;
}

auto MaterializeSplitNode(EmbeddingState& context, const SinkLoadRegionSplitNode& node, int topology_level) -> Pin*
{
  const auto* ports = context.port_table->get(context.split_buffer_master);
  if (ports == nullptr) {
    CTSLOG.error(Loc::current(), "HTree: unresolved ports for split buffer master ", context.split_buffer_master);
  }

  std::unordered_map<const SinkLoadRegionSplitNode*, std::pair<Pin*, Pin*>> node_buffers;
  std::vector<const SinkLoadRegionSplitNode*> pending_nodes = {&node};
  while (!pending_nodes.empty()) {
    const auto* current_node = pending_nodes.back();
    pending_nodes.pop_back();
    if (current_node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null split node during materialization.");
    }

    auto created_buffer
        = CreateBufferInstance(*context.result, context.nextSplitBufferName(), context.split_buffer_master, current_node->center, ports->first, ports->second);
    if (created_buffer.first != nullptr && created_buffer.first->get_inst() != nullptr) {
      context.split_buffer_insts.insert(created_buffer.first->get_inst());
    }
    RecordInsertedInstLevel(*context.result, created_buffer.first == nullptr ? nullptr : created_buffer.first->get_inst(), topology_level,
                            context.split_sub_buffer_count);
    ++context.split_sub_buffer_count;
    node_buffers.emplace(current_node, created_buffer);

    for (std::size_t reverse_index = current_node->children.size(); reverse_index > 0U; --reverse_index) {
      pending_nodes.push_back(&current_node->children.at(reverse_index - 1U));
    }
  }

  struct PendingPostOrderNode
  {
    const SinkLoadRegionSplitNode* node = nullptr;
    bool children_visited = false;
  };

  std::vector<const SinkLoadRegionSplitNode*> post_order_nodes;
  pending_nodes.clear();
  std::vector<PendingPostOrderNode> post_order_stack = {PendingPostOrderNode{.node = &node, .children_visited = false}};
  while (!post_order_stack.empty()) {
    const auto pending = post_order_stack.back();
    post_order_stack.pop_back();
    if (pending.node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null split node during post-order materialization.");
    }
    if (pending.children_visited) {
      post_order_nodes.push_back(pending.node);
      continue;
    }
    post_order_stack.push_back(PendingPostOrderNode{.node = pending.node, .children_visited = true});
    for (std::size_t reverse_index = pending.node->children.size(); reverse_index > 0U; --reverse_index) {
      post_order_stack.push_back(PendingPostOrderNode{.node = &pending.node->children.at(reverse_index - 1U), .children_visited = false});
    }
  }

  for (const auto* current_node : post_order_nodes) {
    const auto buffer_it = node_buffers.find(current_node);
    if (buffer_it == node_buffers.end()) {
      CTSLOG.error(Loc::current(), "HTree: split node buffer was not materialized.");
    }
    if (current_node->children.empty()) {
      CreateNet(*context.result, context.nextNetName(), buffer_it->second.second, current_node->loads, topology_level);
      continue;
    }

    std::vector<Pin*> child_inputs;
    child_inputs.reserve(current_node->children.size());
    for (const auto& child : current_node->children) {
      const auto child_buffer_it = node_buffers.find(&child);
      if (child_buffer_it == node_buffers.end()) {
        CTSLOG.error(Loc::current(), "HTree: split child buffer was not materialized.");
      }
      child_inputs.push_back(child_buffer_it->second.first);
    }
    CreateNet(*context.result, context.nextNetName(), buffer_it->second.second, child_inputs, topology_level);
  }

  const auto root_buffer_it = node_buffers.find(&node);
  if (root_buffer_it == node_buffers.end()) {
    CTSLOG.error(Loc::current(), "HTree: root split buffer was not materialized.");
  }
  return root_buffer_it->second.first;
}

// Materializes the exact recovery plan accepted during legality. Replanning at
// this boundary would allow cap/fanout evidence and committed objects to
// diverge after selection.
auto MaterializeSplitSubBuffers(EmbeddingState& context, std::size_t boundary_node_id, const std::vector<Pin*>& terminal_loads,
                                const Point<int>& upstream_anchor, int topology_level) -> std::vector<Pin*>
{
  if (context.sink_load_region_legality == nullptr) {
    context.result->summary.failure_reason = "missing_selected_sink_load_region_legality";
    return {};
  }
  const SinkLoadRegionSplitPlan* selected_plan = nullptr;
  for (const auto& plan : context.sink_load_region_legality->split_plans) {
    if (plan.boundary_node_id != boundary_node_id) {
      continue;
    }
    if (selected_plan != nullptr) {
      context.result->summary.failure_reason = "duplicate_selected_sink_load_region_plan";
      return {};
    }
    selected_plan = &plan;
  }
  if (selected_plan == nullptr) {
    return terminal_loads;
  }
  switch (ClassifySplitPlanMaterialization(*selected_plan, terminal_loads, upstream_anchor)) {
    case SplitPlanMaterializationDecision::kPassThrough:
      return terminal_loads;
    case SplitPlanMaterializationDecision::kMismatch:
      context.result->summary.failure_reason = "selected_sink_load_region_plan_mismatch";
      return {};
    case SplitPlanMaterializationDecision::kMaterialize:
      break;
  }
  if (context.split_buffer_master.empty()) {
    context.result->summary.failure_reason = "selected_sink_load_region_plan_missing_buffer_master";
    return {};
  }

  std::vector<Pin*> upstream_loads;
  upstream_loads.reserve(selected_plan->children.size());
  for (const auto& child : selected_plan->children) {
    upstream_loads.push_back(MaterializeSplitNode(context, child, topology_level));
  }
  return upstream_loads;
}

auto BuildSegmentObjectsAndGetEntryLoads(EmbeddingState& context, const TreeNode& parent_node, const TreeNode& child_node,
                                         const BufferingPattern& segment_pattern, const std::vector<Pin*>& child_entry_loads, int topology_level)
    -> std::vector<Pin*>
{
  if (child_node.get_loads().empty()) {
    return {};
  }

  const auto& cell_masters = segment_pattern.get_cell_masters();
  const auto& positions = segment_pattern.get_buffer_positions();
  const auto buffer_count = std::min(cell_masters.size(), positions.size());

  std::vector<Pin*> terminal_loads = child_entry_loads;
  if (terminal_loads.empty()) {
    terminal_loads = child_node.get_loads();
  }
  if (terminal_loads.empty()) {
    CTSLOG.error(Loc::current(), "HTree: segment terminal loads are empty for child node ", child_node.get_id());
  }

  const std::size_t built_buffer_count = buffer_count;
  if (built_buffer_count == 0U) {
    return terminal_loads;
  }

  std::vector<std::pair<Pin*, Pin*>> segment_buffers;
  segment_buffers.reserve(built_buffer_count);
  for (std::size_t buffer_index = 0; buffer_index < built_buffer_count; ++buffer_index) {
    const auto* ports = context.port_table->get(cell_masters.at(buffer_index));
    if (ports == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: unresolved ports for edge buffer master ", cell_masters.at(buffer_index));
    }

    const auto buffer_location = InterpolateManhattanPoint(parent_node.get_position(), child_node.get_position(), positions.at(buffer_index));
    auto created_buffer
        = CreateBufferInstance(*context.result, context.nextBufferName(), cell_masters.at(buffer_index), buffer_location, ports->first, ports->second);
    RecordInsertedInstLevel(*context.result, created_buffer.first == nullptr ? nullptr : created_buffer.first->get_inst(), topology_level, buffer_index);
    segment_buffers.push_back(created_buffer);
  }

  for (std::size_t buffer_index = 0; buffer_index + 1U < segment_buffers.size(); ++buffer_index) {
    CreateNet(*context.result, context.nextNetName(), segment_buffers.at(buffer_index).second, std::vector<Pin*>{segment_buffers.at(buffer_index + 1U).first},
              topology_level);
  }

  const auto net_terminal_loads
      = MaterializeSplitSubBuffers(context, child_node.get_id(), terminal_loads, segment_buffers.back().second->get_location(), topology_level);
  if (net_terminal_loads.empty()) {
    return {};
  }
  CreateNet(*context.result, context.nextNetName(), segment_buffers.back().second, net_terminal_loads, topology_level);
  return std::vector<Pin*>{segment_buffers.front().first};
}

}  // namespace

auto InterpolateManhattanPoint(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>
{
  const double clamped_position = std::clamp(normalized_position, 0.0, 1.0);
  const int dx = sink.get_x() - source.get_x();
  const int dy = sink.get_y() - source.get_y();
  const int total_distance = std::abs(dx) + std::abs(dy);
  if (total_distance == 0) {
    return source;
  }

  const int target_distance = static_cast<int>(std::lround(clamped_position * static_cast<double>(total_distance)));
  const int x_step = std::min(std::abs(dx), target_distance);
  const int y_step = std::max(0, target_distance - x_step);
  const int x = source.get_x() + ((dx >= 0) ? x_step : -x_step);
  const int y = source.get_y() + ((dy >= 0) ? y_step : -y_step);
  return Point<int>(x, y);
}

auto ValidateRootDriverSizing(Design& design, Wrapper& wrapper, const HTree::Build& result, const std::string& cell_master) -> bool
{
  if (cell_master.empty() || result.output.root_inst == nullptr) {
    return true;
  }

  auto* output_pin = result.output.root_output_pin;
  auto* input_pin = FindUniqueTypedPin(result.output.root_inst, false);
  if (output_pin == nullptr || output_pin->get_inst() != result.output.root_inst || input_pin == nullptr) {
    CTSLOG.warn(Loc::current(), "HTree: cannot apply selected root driver master ", cell_master,
                " because the input root net driver inst does not expose a complete buffer pin pair.");
    return false;
  }

  const auto ports = ResolveBufferPorts(wrapper, cell_master);
  if (!ports.has_value()) {
    CTSLOG.warn(Loc::current(), "HTree: cannot apply selected root driver master ", cell_master, " because its buffer ports could not be resolved.");
    return false;
  }

  if (!CanRenameRootDriverPin(design, input_pin, ports->first) || !CanRenameRootDriverPin(design, output_pin, ports->second)) {
    CTSLOG.warn(Loc::current(), "HTree: cannot apply selected root driver master ", cell_master,
                " because renamed root driver pins would conflict with the Design pin index.");
    return false;
  }

  return true;
}

auto ApplyRootDriverSizing(Design& design, Wrapper& wrapper, htree::DiagnosticBuild& result, const std::string& cell_master) -> bool
{
  if (cell_master.empty() || result.output.root_inst == nullptr) {
    return true;
  }

  auto* output_pin = result.output.root_output_pin;
  auto* input_pin = FindUniqueTypedPin(result.output.root_inst, false);
  const auto ports = ResolveBufferPorts(wrapper, cell_master);
  if (output_pin == nullptr || output_pin->get_inst() != result.output.root_inst || input_pin == nullptr || !ports.has_value()) {
    return false;
  }

  const std::string old_input_pin_name = input_pin->get_name();
  if (!RenameRootDriverPin(design, input_pin, ports->first)) {
    return false;
  }

  if (!RenameRootDriverPin(design, output_pin, ports->second)) {
    if (!RenameRootDriverPin(design, input_pin, old_input_pin_name)) {
      CTSLOG.error(Loc::current(), "HTree: failed to roll back root driver input-pin rename.");
    }
    return false;
  }

  result.output.root_inst->set_cell_master(cell_master);
  result.output.root_inst->set_type(InstType::kBuffer);
  input_pin->set_type(PinType::kIn);
  output_pin->set_type(PinType::kOut);
  result.output.root_inst->add_pin(output_pin);
  result.output.root_input_pin = input_pin;
  result.output.root_output_pin = output_pin;
  result.diagnostics.selected_root_driver_cell_master = result.output.root_inst->get_cell_master();
  return true;
}

auto BuildEmbedding(Wrapper& wrapper, htree::DiagnosticBuild& result, const BufferPatternLibrary& segment_pattern_library,
                    const SinkLoadRegionLegalitySummary& sink_load_region_legality) -> void
{
  if (!result.output.best_pattern.has_value()) {
    return;
  }
  if (!sink_load_region_legality.legal) {
    result.summary.failure_reason = "selected_sink_load_region_legality_invalid";
    return;
  }

  const auto topology_levels = result.output.topology.levels();
  const std::size_t candidate_level_count = result.output.levels.size();
  if (topology_levels.size() <= 1U || candidate_level_count == 0U || candidate_level_count >= topology_levels.size()) {
    result.summary.failure_reason = "invalid_embedding_depth";
    return;
  }

  BufferPortTable port_table(wrapper);
  std::vector<PatternId> level_segment_pattern_ids = result.output.best_pattern->get_level_segment_pattern_ids();
  if (level_segment_pattern_ids.size() != result.output.levels.size()) {
    CTSLOG.error(Loc::current(), "HTree: best topology pattern levels do not match planned H-tree levels.");
  }

  std::vector<const BufferingPattern*> level_patterns;
  level_patterns.reserve(level_segment_pattern_ids.size());
  for (const auto pattern_id : level_segment_pattern_ids) {
    const auto* level_pattern = segment_pattern_library.find(pattern_id);
    if (level_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: selected segment pattern metadata is missing.");
    }
    level_patterns.push_back(level_pattern);
  }

  const auto* root_node = result.output.topology.get_node(result.output.topology.get_root());
  if (root_node == nullptr) {
    CTSLOG.error(Loc::current(), "HTree: topology root is missing during embedding construction.");
  }

  if (result.output.root_net == nullptr) {
    CTSLOG.error(Loc::current(), "HTree: input root net is missing during embedding construction.");
  }
  if (result.output.root_output_pin == nullptr) {
    CTSLOG.error(Loc::current(), "HTree: input root net driver pin is missing during embedding construction.");
  }
  result.output.root_inst = result.output.root_output_pin->get_inst();
  result.output.root_input_pin = FindUniqueTypedPin(result.output.root_inst, false);

  EmbeddingState context{
      .result = &result,
      .port_table = &port_table,
      .object_name_prefix = result.diagnostics.object_name_prefix,
      .sink_load_region_legality = &sink_load_region_legality,
      .split_buffer_master = result.diagnostics.split_buffer_cell_master,
      .split_buffer_insts = {},
  };

  std::unordered_map<std::size_t, std::vector<Pin*>> entry_loads_by_node;
  entry_loads_by_node.reserve(result.output.topology.get_size());

  for (const auto node_id : topology_levels.at(candidate_level_count)) {
    const auto* node = result.output.topology.get_node(node_id);
    if (node == nullptr || node->get_loads().empty()) {
      continue;
    }
    entry_loads_by_node[node_id] = node->get_loads();
  }

  for (std::size_t reverse_depth = candidate_level_count; reverse_depth > 0U; --reverse_depth) {
    const std::size_t depth = reverse_depth - 1U;
    const auto* segment_pattern = level_patterns.at(depth);
    if (segment_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: missing selected segment pattern metadata during embedding construction.");
    }

    for (const auto node_id : topology_levels.at(depth)) {
      const auto* node = result.output.topology.get_node(node_id);
      if (node == nullptr || node->get_loads().empty()) {
        continue;
      }

      std::vector<Pin*> node_entry_loads;
      for (const auto child_id : node->get_children()) {
        if (child_id == std::numeric_limits<std::size_t>::max()) {
          continue;
        }

        const auto* child_node = result.output.topology.get_node(child_id);
        if (child_node == nullptr || child_node->get_loads().empty()) {
          continue;
        }

        const auto child_it = entry_loads_by_node.find(child_id);
        const auto& child_entry_loads = (child_it != entry_loads_by_node.end()) ? child_it->second : child_node->get_loads();
        auto segment_entry_loads
            = BuildSegmentObjectsAndGetEntryLoads(context, *node, *child_node, *segment_pattern, child_entry_loads, static_cast<int>(depth));
        node_entry_loads.insert(node_entry_loads.end(), segment_entry_loads.begin(), segment_entry_loads.end());
      }

      if (node_entry_loads.empty()) {
        node_entry_loads = node->get_loads();
      }
      entry_loads_by_node[node_id] = std::move(node_entry_loads);
    }
  }

  const auto root_it = entry_loads_by_node.find(result.output.topology.get_root());
  auto root_entry_loads = (root_it != entry_loads_by_node.end()) ? root_it->second : root_node->get_loads();
  if (root_entry_loads.empty()) {
    CTSLOG.error(Loc::current(), "HTree: root entry loads are empty during embedding construction.");
  }
  root_entry_loads = MaterializeSplitSubBuffers(context, root_node->get_id(), root_entry_loads, result.output.root_output_pin->get_location(), 0);
  if (root_entry_loads.empty()) {
    return;
  }
  ConnectNet(result.output.root_net, result.output.root_output_pin, root_entry_loads);
  const auto planned_split_buffer_count
      = std::accumulate(sink_load_region_legality.split_plans.begin(), sink_load_region_legality.split_plans.end(), std::size_t{0U},
                        [](std::size_t count, const SinkLoadRegionSplitPlan& plan) -> std::size_t { return count + (plan.required ? plan.buffer_count : 0U); });
  if (context.split_sub_buffer_count != planned_split_buffer_count || context.split_buffer_insts.size() != planned_split_buffer_count) {
    result.summary.failure_reason = "sink_load_region_plan_materialization_count_mismatch";
    return;
  }
  result.diagnostics.pruned_leaf_single_load_buffers = PruneLeafSingleLoadBuffers(result, context.split_buffer_insts);
  result.diagnostics.embedded_split_sub_buffer_count = context.split_sub_buffer_count;
}

}  // namespace icts::htree
