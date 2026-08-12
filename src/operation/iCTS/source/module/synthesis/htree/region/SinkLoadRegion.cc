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
 * @file SinkLoadRegion.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Sink-load-region legality and boundary-cap coverage checks for H-tree candidates.
 */

#include "synthesis/htree/region/SinkLoadRegion.hh"

#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "ClockRouteSegmentRC.hh"
#include "Clustering.hh"
#include "FastClustering.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Logger.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "Tree.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"

namespace icts::htree {
namespace {

auto CalcMedianCapPf(std::vector<double> caps_pf) -> double
{
  if (caps_pf.empty()) {
    return 0.0;
  }

  std::ranges::sort(caps_pf);
  const std::size_t middle = caps_pf.size() / 2U;
  if ((caps_pf.size() & 1U) == 1U) {
    return caps_pf.at(middle);
  }
  return (caps_pf.at(middle - 1U) + caps_pf.at(middle)) * 0.5;
}

auto BuildCapDistributionStats(const std::vector<double>& caps_pf) -> CapDistributionStats
{
  CapDistributionStats stats;
  if (caps_pf.empty()) {
    return stats;
  }

  stats.group_count = caps_pf.size();
  stats.cap_min_pf = *std::ranges::min_element(caps_pf);
  stats.cap_max_pf = *std::ranges::max_element(caps_pf);

  double total_cap_pf = 0.0;
  for (const double cap_pf : caps_pf) {
    total_cap_pf += cap_pf;
  }
  stats.cap_mean_pf = total_cap_pf / static_cast<double>(caps_pf.size());
  stats.cap_median_pf = CalcMedianCapPf(caps_pf);
  return stats;
}

struct SinkPinCapCollection
{
  std::unordered_map<const Pin*, double> cap_pf_by_pin;
  std::string failure_reason;

  auto ok() const -> bool { return failure_reason.empty(); }
};

auto collectSinkPinCapPfByPin(Wrapper& wrapper, const std::vector<SinkLoadRegionBoundaryGroup>& groups) -> SinkPinCapCollection
{
  SinkPinCapCollection collection;
  auto& sink_pin_cap_pf_by_pin = collection.cap_pf_by_pin;
  for (const auto& group : groups) {
    if (group.loads == nullptr) {
      continue;
    }
    sink_pin_cap_pf_by_pin.reserve(sink_pin_cap_pf_by_pin.size() + group.loads->size());
    for (const auto* pin : *group.loads) {
      if (pin == nullptr) {
        continue;
      }
      const auto pin_cap_pf = wrapper.queryPinCapacitance(pin);
      if (!pin_cap_pf.has_value()) {
        collection.failure_reason = "unavailable_sink_pin_cap:" + Design::getPinFullName(pin);
        return collection;
      }
      sink_pin_cap_pf_by_pin[pin] = *pin_cap_pf;
    }
  }
  return collection;
}

struct LeafElectricalConfigBuild
{
  std::optional<ClusterConfig> config = std::nullopt;
  std::string failure_reason;
};

auto BuildLeafElectricalConfig(const SinkLoadRegionLegalityInput& input, const std::vector<SinkLoadRegionBoundaryGroup>& groups) -> LeafElectricalConfigBuild
{
  const double max_cap = input.has_max_cap ? input.max_cap_pf : std::numeric_limits<double>::infinity();
  if (input.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "HTree: Wrapper is unavailable for sink-load-region legality.");
  }
  auto config = FastClustering::buildElectricalBaseConfig(input.max_fanout, max_cap);
  config.clock_route_segment_rc = input.clock_route_segment_rc;
  const auto sink_pin_cap_pf_by_pin = collectSinkPinCapPfByPin(*input.wrapper, groups);
  if (!sink_pin_cap_pf_by_pin.ok()) {
    return LeafElectricalConfigBuild{.failure_reason = sink_pin_cap_pf_by_pin.failure_reason};
  }
  config.sink_pin_cap_pf_by_pin = sink_pin_cap_pf_by_pin.cap_pf_by_pin;
  config.enable_exact_cap = true;
  config.always_build_exact_cap = true;
  config.scoring_strategy = ClusterScoringStrategy::kTotalWirelength;
  return LeafElectricalConfigBuild{.config = std::move(config), .failure_reason = {}};
}

auto ResolveBottomMostBufferedLevel(const HTreeTopologyPattern& topology_pattern, const BufferPatternLibrary& segment_pattern_library) -> int
{
  const auto& level_segment_pattern_ids = topology_pattern.get_level_segment_pattern_ids();
  for (int level = static_cast<int>(level_segment_pattern_ids.size()) - 1; level >= 0; --level) {
    const auto segment_pattern_id = level_segment_pattern_ids.at(static_cast<std::size_t>(level));
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    if (segment_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: missing segment pattern metadata while resolving sink-load-region boundary.");
    }
    if (!segment_pattern->get_buffer_positions().empty()) {
      return level;
    }
  }
  return -1;
}

auto ResolveSinkLoadRegionLegalitySignature(const HTreeTopologyPattern& topology_pattern, const BufferPatternLibrary& segment_pattern_library)
    -> SinkLoadRegionLegalitySignature
{
  const int bottom_most_buffered_level = ResolveBottomMostBufferedLevel(topology_pattern, segment_pattern_library);
  SinkLoadRegionLegalitySignature signature;
  signature.bottom_most_buffered_level = bottom_most_buffered_level;
  if (bottom_most_buffered_level >= 0) {
    signature.segment_pattern_id = topology_pattern.get_level_segment_pattern_ids().at(static_cast<std::size_t>(bottom_most_buffered_level));
  }
  return signature;
}

auto BuildSinkLoadRegionFeasibilityReason(std::size_t node_id, const Point<int>& anchor, const std::string& detail) -> std::string
{
  std::ostringstream stream;
  stream << "htree_load_group_node_" << node_id << " anchor=(" << anchor.get_x() << "," << anchor.get_y() << ") " << detail;
  return stream.str();
}

auto InterpolateBoundaryAnchor(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>
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

struct SinkLoadRegionBoundaryCollection
{
  std::vector<SinkLoadRegionBoundaryGroup> groups;
  SinkLoadRegionViolation violation = SinkLoadRegionViolation::kNone;
  std::string failure_reason;
};

auto MakeBoundaryCollectionFailure(std::vector<SinkLoadRegionBoundaryGroup> groups, SinkLoadRegionViolation violation, std::string reason)
    -> SinkLoadRegionBoundaryCollection
{
  return SinkLoadRegionBoundaryCollection{
      .groups = std::move(groups),
      .violation = violation,
      .failure_reason = std::move(reason),
  };
}

auto CollectSinkLoadRegionBoundaryGroups(const Tree& topology, const SinkLoadRegionLegalitySignature& signature,
                                         const BufferPatternLibrary& segment_pattern_library) -> SinkLoadRegionBoundaryCollection
{
  std::vector<SinkLoadRegionBoundaryGroup> groups;

  const auto* root_node = topology.get_node(topology.get_root());
  if (root_node == nullptr) {
    return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kMissingTopologyRoot, "missing_topology_root");
  }

  if (signature.bottom_most_buffered_level < 0) {
    if (root_node->get_loads().empty()) {
      return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kEmptyLoadGroup, "empty_root_load_group");
    }
    groups.push_back(SinkLoadRegionBoundaryGroup{
        .node_id = root_node->get_id(),
        .anchor = root_node->get_position(),
        .loads = &root_node->get_loads(),
    });
    return SinkLoadRegionBoundaryCollection{
        .groups = std::move(groups),
        .violation = SinkLoadRegionViolation::kNone,
        .failure_reason = {},
    };
  }

  const auto topology_levels = topology.levels();
  const std::size_t boundary_level = static_cast<std::size_t>(signature.bottom_most_buffered_level) + 1U;
  if (boundary_level >= topology_levels.size()) {
    return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kMissingTopologyLevel, "missing_sink_load_region_boundary_level");
  }

  const auto* segment_pattern = segment_pattern_library.find(signature.segment_pattern_id);
  if (segment_pattern == nullptr) {
    return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kMissingSegmentPattern, "missing_boundary_segment_pattern");
  }
  if (segment_pattern->get_buffer_positions().empty()) {
    return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kMissingBufferPosition, "missing_boundary_buffer_position");
  }

  const double last_buffer_position = segment_pattern->get_buffer_positions().back();
  groups.reserve(topology_levels.at(boundary_level).size());
  for (const auto node_id : topology_levels.at(boundary_level)) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr) {
      return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kMissingTopologyNode, "missing_boundary_topology_node");
    }
    if (node->get_loads().empty()) {
      continue;
    }

    const auto* parent_node = topology.get_node(node->get_parent());
    if (parent_node == nullptr) {
      return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kMissingTopologyNode, "missing_boundary_parent_node");
    }

    groups.push_back(SinkLoadRegionBoundaryGroup{
        .node_id = node_id,
        .anchor = InterpolateBoundaryAnchor(parent_node->get_position(), node->get_position(), last_buffer_position),
        .loads = &node->get_loads(),
    });
  }

  if (groups.empty()) {
    return MakeBoundaryCollectionFailure(std::move(groups), SinkLoadRegionViolation::kEmptyLoadGroup, "empty_sink_load_region_groups");
  }
  return SinkLoadRegionBoundaryCollection{
      .groups = std::move(groups),
      .violation = SinkLoadRegionViolation::kNone,
      .failure_reason = {},
  };
}

struct SplitElectricalCheck
{
  bool legal = true;
  SinkLoadRegionViolation violation = SinkLoadRegionViolation::kNone;
  std::string detail;
  double root_cap_pf = 0.0;
};

auto CalcSplitChildrenCapPf(const Point<int>& anchor, const std::vector<SinkLoadRegionSplitNode>& children, const SinkLoadRegionLegalityInput& input) -> double
{
  double cap_pf = static_cast<double>(children.size()) * std::max(0.0, input.split_buffer_input_cap_pf);
  if (input.clock_route_segment_rc.dbu_per_um <= 0) {
    return cap_pf;
  }
  for (const auto& child : children) {
    const auto distance_dbu = std::llabs(static_cast<long long>(child.center.get_x()) - static_cast<long long>(anchor.get_x()))
                              + std::llabs(static_cast<long long>(child.center.get_y()) - static_cast<long long>(anchor.get_y()));
    cap_pf += static_cast<double>(distance_dbu) / static_cast<double>(input.clock_route_segment_rc.dbu_per_um)
              * input.clock_route_segment_rc.capacitance_per_um_pf;
  }
  return cap_pf;
}

auto CheckSplitChildrenElectrical(const Point<int>& anchor, const std::vector<SinkLoadRegionSplitNode>& children, const SinkLoadRegionLegalityInput& input,
                                  const char* scope) -> SplitElectricalCheck
{
  SplitElectricalCheck check;
  if (input.max_fanout > 0U && children.size() > input.max_fanout) {
    std::ostringstream detail;
    detail << scope << "_fanout_violation child_count=" << children.size() << ", max_fanout=" << input.max_fanout;
    check.legal = false;
    check.violation = SinkLoadRegionViolation::kFanout;
    check.detail = detail.str();
    return check;
  }

  check.root_cap_pf = CalcSplitChildrenCapPf(anchor, children, input);
  if (input.has_max_cap && check.root_cap_pf > input.max_cap_pf) {
    std::ostringstream detail;
    detail << scope << "_cap_violation total_cap_pf=" << check.root_cap_pf << ", max_cap_pf=" << input.max_cap_pf;
    check.legal = false;
    check.violation = SinkLoadRegionViolation::kCapacitance;
    check.detail = detail.str();
  }
  return check;
}

auto EvaluateSplitNodeElectrical(const SinkLoadRegionSplitNode& node, const ClusterConfig& electrical_config,
                                 const SinkLoadRegionLegalityContext& legality_context) -> SplitElectricalCheck
{
  struct PendingSplitNode
  {
    const SinkLoadRegionSplitNode* node = nullptr;
    std::string detail_prefix;
  };

  std::vector<PendingSplitNode> pending_nodes = {PendingSplitNode{.node = &node, .detail_prefix = {}}};
  while (!pending_nodes.empty()) {
    const auto pending = std::move(pending_nodes.back());
    pending_nodes.pop_back();
    const auto* current_node = pending.node;
    if (current_node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null split node during electrical evaluation.");
    }

    if (!current_node->children.empty()) {
      auto check = CheckSplitChildrenElectrical(current_node->center, current_node->children, legality_context.input, "split_internal");
      if (!check.legal) {
        check.detail = pending.detail_prefix + check.detail;
        return check;
      }
      for (std::size_t reverse_index = current_node->children.size(); reverse_index > 0U; --reverse_index) {
        const auto child_index = reverse_index - 1U;
        std::ostringstream prefix;
        prefix << pending.detail_prefix << "split_child=" << child_index << " ";
        pending_nodes.push_back(PendingSplitNode{.node = &current_node->children.at(child_index), .detail_prefix = prefix.str()});
      }
      continue;
    }

    const auto exact = Clustering::evaluateClusterElectrical(current_node->loads, current_node->center, electrical_config, true);
    if (exact.legal) {
      continue;
    }
    SplitElectricalCheck check;
    check.legal = false;
    if (exact.violation == ClusterElectricalViolation::kRoutingFailed) {
      check.violation = SinkLoadRegionViolation::kRoutingFailed;
      check.detail = "split_leaf_routing_failed";
    } else if (exact.violation == ClusterElectricalViolation::kFanout) {
      std::ostringstream detail;
      detail << "split_leaf_fanout_violation load_count=" << current_node->loads.size() << ", max_fanout=" << legality_context.input.max_fanout;
      check.violation = SinkLoadRegionViolation::kFanout;
      check.detail = detail.str();
    } else if (exact.violation == ClusterElectricalViolation::kCapacitance) {
      std::ostringstream detail;
      detail << "split_leaf_cap_violation load_count=" << current_node->loads.size() << ", total_cap_pf=" << exact.summary.total_cap_pf;
      if (legality_context.input.has_max_cap) {
        detail << ", max_cap_pf=" << legality_context.input.max_cap_pf;
      }
      check.violation = SinkLoadRegionViolation::kCapacitance;
      check.detail = detail.str();
    } else {
      check.violation = SinkLoadRegionViolation::kEmptyLoadGroup;
      check.detail = "split_leaf_evaluation_failed";
    }
    check.detail = pending.detail_prefix + check.detail;
    return check;
  }
  return {};
}

auto EvaluateSplitPlanElectrical(const SinkLoadRegionSplitPlan& split_plan, const Point<int>& anchor, const ClusterConfig& electrical_config,
                                 const SinkLoadRegionLegalityContext& legality_context) -> SplitElectricalCheck
{
  auto root_check = CheckSplitChildrenElectrical(anchor, split_plan.children, legality_context.input, "split_root");
  if (!root_check.legal) {
    return root_check;
  }
  for (std::size_t child_index = 0; child_index < split_plan.children.size(); ++child_index) {
    auto child_check = EvaluateSplitNodeElectrical(split_plan.children.at(child_index), electrical_config, legality_context);
    if (!child_check.legal) {
      std::ostringstream detail;
      detail << "split_child=" << child_index << " " << child_check.detail;
      child_check.detail = detail.str();
      return child_check;
    }
  }
  return root_check;
}

auto EvaluateSinkLoadRegionLegality(const Tree& topology, const SinkLoadRegionLegalitySignature& signature, const BufferPatternLibrary& segment_pattern_library,
                                    const SinkLoadRegionLegalityContext& legality_context) -> SinkLoadRegionLegalitySummary
{
  SinkLoadRegionLegalitySummary result;
  result.bottom_most_buffered_level = signature.bottom_most_buffered_level;
  result.segment_pattern_id = signature.segment_pattern_id;

  const auto collection = CollectSinkLoadRegionBoundaryGroups(topology, signature, segment_pattern_library);
  if (collection.violation != SinkLoadRegionViolation::kNone) {
    result.failure_reason = collection.failure_reason;
    result.violation = collection.violation;
    return result;
  }

  const auto electrical_config = BuildLeafElectricalConfig(legality_context.input, collection.groups);
  if (!electrical_config.config.has_value()) {
    result.failure_reason = electrical_config.failure_reason.empty() ? "sink_pin_capacitance_unavailable" : electrical_config.failure_reason;
    result.violation = SinkLoadRegionViolation::kPinCapUnavailable;
    result.monotone_hard_fail = false;
    return result;
  }
  const std::size_t max_fanout = legality_context.input.max_fanout;
  std::vector<SinkLoadRegionSplitPlan> group_split_plans(collection.groups.size());
  for (std::size_t group_index = 0; group_index < collection.groups.size(); ++group_index) {
    const auto& group = collection.groups.at(group_index);
    const auto* loads = group.loads;
    if (loads == nullptr || loads->empty()) {
      result.violation = SinkLoadRegionViolation::kEmptyLoadGroup;
      result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, "empty_group_loads");
      return result;
    }
    if (max_fanout > 0U && loads->size() > max_fanout) {
      auto split_plan = SplitSinkLoadRegionGroup(*loads, max_fanout);
      if (!split_plan.feasible) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout << ", split=infeasible";
        result.violation = SinkLoadRegionViolation::kFanout;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, detail.str());
        return result;
      }
      group_split_plans.at(group_index) = std::move(split_plan);
      continue;
    }

    const auto lower_bound = Clustering::evaluateClusterElectrical(*loads, group.anchor, *electrical_config.config, false);
    if (!lower_bound.legal) {
      if (lower_bound.violation == ClusterElectricalViolation::kFanout) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
        result.violation = SinkLoadRegionViolation::kFanout;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else if (lower_bound.violation == ClusterElectricalViolation::kCapacitance) {
        std::ostringstream detail;
        detail << "pin_cap_lower_bound_violation total_cap_pf=" << lower_bound.summary.total_cap_pf;
        if (legality_context.input.has_max_cap) {
          detail << ", max_cap_pf=" << legality_context.input.max_cap_pf;
        }
        result.violation = SinkLoadRegionViolation::kPinCapLowerBound;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else {
        result.violation = SinkLoadRegionViolation::kEmptyLoadGroup;
        result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, "lower_bound_evaluation_failed");
      }
      return result;
    }
  }

  std::vector<double> total_caps_pf;
  total_caps_pf.reserve(collection.groups.size());
  std::size_t split_group_count = 0U;
  std::size_t split_extra_buffer_count = 0U;
  unsigned split_local_depth = 0U;
  for (std::size_t group_index = 0; group_index < collection.groups.size(); ++group_index) {
    const auto& group = collection.groups.at(group_index);
    const auto* loads = group.loads;
    if (loads == nullptr || loads->empty()) {
      CTSLOG.error(Loc::current(), "HTree: sink-load-region boundary group lost its load set.");
    }

    const auto& split_plan = group_split_plans.at(group_index);
    if (!split_plan.feasible) {
      const auto exact = Clustering::evaluateClusterElectrical(*loads, group.anchor, *electrical_config.config, true);
      if (!exact.legal) {
        if (exact.violation == ClusterElectricalViolation::kRoutingFailed) {
          result.violation = SinkLoadRegionViolation::kRoutingFailed;
          result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, "routing_failed");
        } else if (exact.violation == ClusterElectricalViolation::kCapacitance) {
          std::ostringstream detail;
          detail << "cap_violation total_cap_pf=" << exact.summary.total_cap_pf;
          if (legality_context.input.has_max_cap) {
            detail << ", max_cap_pf=" << legality_context.input.max_cap_pf;
          }
          result.violation = SinkLoadRegionViolation::kCapacitance;
          result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, detail.str());
        } else if (exact.violation == ClusterElectricalViolation::kFanout) {
          std::ostringstream detail;
          detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
          result.violation = SinkLoadRegionViolation::kFanout;
          result.monotone_hard_fail = true;
          result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, detail.str());
        } else {
          result.violation = SinkLoadRegionViolation::kEmptyLoadGroup;
          result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, "exact_evaluation_failed");
        }
        return result;
      }
      total_caps_pf.push_back(exact.summary.total_cap_pf);
      continue;
    }

    const auto split_check = EvaluateSplitPlanElectrical(split_plan, group.anchor, *electrical_config.config, legality_context);
    if (!split_check.legal) {
      std::ostringstream detail;
      detail << "split_tree_violation local_depth=" << split_plan.local_depth << ", buffer_count=" << split_plan.buffer_count << ", " << split_check.detail;
      result.violation = split_check.violation;
      result.monotone_hard_fail = split_check.violation == SinkLoadRegionViolation::kFanout;
      result.failure_reason = BuildSinkLoadRegionFeasibilityReason(group.node_id, group.anchor, detail.str());
      return result;
    }
    total_caps_pf.push_back(split_check.root_cap_pf);
    ++split_group_count;
    split_extra_buffer_count += split_plan.buffer_count;
    split_local_depth = std::max(split_local_depth, split_plan.local_depth);
  }

  result.split_group_count = split_group_count;
  result.split_extra_buffer_count = split_extra_buffer_count;
  result.split_local_depth = split_local_depth;
  result.cap_distribution = BuildCapDistributionStats(total_caps_pf);
  result.required_leaf_load_cap_pf = result.cap_distribution.cap_max_pf;
  result.required_leaf_load_cap_covering_idx = CoveringBoundaryIndex(result.required_leaf_load_cap_pf, legality_context.cap_lattice);
  result.violation = SinkLoadRegionViolation::kNone;
  result.legal = true;
  return result;
}

auto CanonicalLoadLess(const Pin* lhs, const Pin* rhs) -> bool
{
  const auto& lhs_location = lhs->get_location();
  const auto& rhs_location = rhs->get_location();
  if (lhs_location.get_x() != rhs_location.get_x()) {
    return lhs_location.get_x() < rhs_location.get_x();
  }
  if (lhs_location.get_y() != rhs_location.get_y()) {
    return lhs_location.get_y() < rhs_location.get_y();
  }
  return lhs->get_name() < rhs->get_name();
}

auto CalcLoadCenter(const std::vector<Pin*>& loads) -> Point<int>
{
  if (loads.empty()) {
    return Point<int>(0, 0);
  }
  long long sum_x = 0;
  long long sum_y = 0;
  for (const auto* pin : loads) {
    sum_x += pin->get_location().get_x();
    sum_y += pin->get_location().get_y();
  }
  const auto count = static_cast<double>(loads.size());
  return Point<int>(static_cast<int>(std::llround(static_cast<double>(sum_x) / count)), static_cast<int>(std::llround(static_cast<double>(sum_y) / count)));
}

auto SortLoadsForLocalSplit(std::vector<Pin*>& loads) -> void
{
  int min_x = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int min_y = std::numeric_limits<int>::max();
  int max_y = std::numeric_limits<int>::min();
  for (const auto* pin : loads) {
    const auto& location = pin->get_location();
    min_x = std::min(min_x, location.get_x());
    max_x = std::max(max_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_y = std::max(max_y, location.get_y());
  }
  const bool split_on_x = (static_cast<long long>(max_x) - min_x) >= (static_cast<long long>(max_y) - min_y);
  std::ranges::sort(loads, [split_on_x](const Pin* lhs, const Pin* rhs) -> bool {
    const auto lhs_axis = split_on_x ? lhs->get_location().get_x() : lhs->get_location().get_y();
    const auto rhs_axis = split_on_x ? rhs->get_location().get_x() : rhs->get_location().get_y();
    if (lhs_axis != rhs_axis) {
      return lhs_axis < rhs_axis;
    }
    return CanonicalLoadLess(lhs, rhs);
  });
}

auto BuildLocalSplitBaseNode(std::vector<Pin*> loads) -> SinkLoadRegionSplitNode
{
  std::ranges::sort(loads, CanonicalLoadLess);
  SinkLoadRegionSplitNode node{
      .loads = std::move(loads),
      .center = {},
      .children = {},
  };
  node.center = CalcLoadCenter(node.loads);
  return node;
}

auto CalcLocalSplitLeafCapacity(unsigned depth, std::size_t max_fanout) -> std::size_t
{
  std::size_t capacity = 1U;
  for (unsigned level = 0U; level < depth; ++level) {
    if (capacity > std::numeric_limits<std::size_t>::max() / max_fanout) {
      return std::numeric_limits<std::size_t>::max();
    }
    capacity *= max_fanout;
  }
  return capacity;
}

auto ResolveUniformLocalSplitDepth(std::size_t leaf_count, std::size_t max_fanout) -> unsigned
{
  unsigned depth = 1U;
  auto capacity = max_fanout;
  while (capacity < leaf_count) {
    if (capacity > std::numeric_limits<std::size_t>::max() / max_fanout) {
      return depth;
    }
    capacity *= max_fanout;
    ++depth;
  }
  return depth;
}

auto ResolveUniformLocalSplitChildCount(std::size_t leaf_count, std::size_t max_fanout, unsigned depth) -> std::size_t
{
  if (depth <= 1U) {
    return leaf_count;
  }
  const auto child_capacity = CalcLocalSplitLeafCapacity(depth - 1U, max_fanout);
  return std::min(max_fanout, (leaf_count + child_capacity - 1U) / child_capacity);
}

struct LocalSplitBuildFrame
{
  SinkLoadRegionSplitNode* node = nullptr;
  std::vector<Pin*> loads;
  std::size_t leaf_count = 0U;
  unsigned depth = 0U;
};

auto AppendUniformLocalSplitChildren(SinkLoadRegionSplitNode& root, std::vector<Pin*> loads, std::size_t leaf_count, std::size_t max_fanout, unsigned depth)
    -> void
{
  std::vector<LocalSplitBuildFrame> pending_frames;
  pending_frames.push_back(LocalSplitBuildFrame{
      .node = &root,
      .loads = std::move(loads),
      .leaf_count = leaf_count,
      .depth = depth,
  });

  while (!pending_frames.empty()) {
    auto frame = std::move(pending_frames.back());
    pending_frames.pop_back();
    if (frame.node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null local split frame node.");
    }
    if (frame.leaf_count == 0U || frame.depth == 0U) {
      continue;
    }

    SortLoadsForLocalSplit(frame.loads);
    const auto child_count = ResolveUniformLocalSplitChildCount(frame.leaf_count, max_fanout, frame.depth);
    const auto child_depth = frame.depth - 1U;
    frame.node->children.reserve(child_count);
    std::vector<std::size_t> child_leaf_counts;
    child_leaf_counts.reserve(child_count);

    for (std::size_t child_index = 0U; child_index < child_count; ++child_index) {
      const auto leaf_begin = (frame.leaf_count * child_index) / child_count;
      const auto leaf_end = (frame.leaf_count * (child_index + 1U)) / child_count;
      const auto begin_index = (frame.loads.size() * leaf_begin) / frame.leaf_count;
      const auto end_index = (frame.loads.size() * leaf_end) / frame.leaf_count;
      std::vector<Pin*> child_loads(frame.loads.begin() + static_cast<std::ptrdiff_t>(begin_index),
                                    frame.loads.begin() + static_cast<std::ptrdiff_t>(end_index));
      child_leaf_counts.push_back(leaf_end - leaf_begin);
      frame.node->children.push_back(BuildLocalSplitBaseNode(std::move(child_loads)));
    }

    if (child_depth == 0U) {
      continue;
    }
    for (std::size_t child_index = frame.node->children.size(); child_index > 0U; --child_index) {
      auto& child = frame.node->children.at(child_index - 1U);
      pending_frames.push_back(LocalSplitBuildFrame{
          .node = &child,
          .loads = child.loads,
          .leaf_count = child_leaf_counts.at(child_index - 1U),
          .depth = child_depth,
      });
    }
  }
}

auto BuildLocalSplitNode(std::vector<Pin*> loads, std::size_t max_fanout) -> SinkLoadRegionSplitNode
{
  auto node = BuildLocalSplitBaseNode(std::move(loads));
  if (node.loads.size() <= max_fanout) {
    return node;
  }

  const auto leaf_count = (node.loads.size() + max_fanout - 1U) / max_fanout;
  const auto split_depth = ResolveUniformLocalSplitDepth(leaf_count, max_fanout);
  auto ordered_loads = node.loads;
  AppendUniformLocalSplitChildren(node, std::move(ordered_loads), leaf_count, max_fanout, split_depth);
  return node;
}

auto CountSplitBuffers(const std::vector<SinkLoadRegionSplitNode>& nodes) -> std::size_t
{
  std::size_t count = 0U;
  std::vector<const SinkLoadRegionSplitNode*> pending_nodes;
  pending_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    pending_nodes.push_back(&node);
  }
  while (!pending_nodes.empty()) {
    const auto* node = pending_nodes.back();
    pending_nodes.pop_back();
    if (node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null split node during buffer counting.");
    }
    ++count;
    for (const auto& child : node->children) {
      pending_nodes.push_back(&child);
    }
  }
  return count;
}

auto CountSplitLeaves(const std::vector<SinkLoadRegionSplitNode>& nodes) -> std::size_t
{
  std::size_t count = 0U;
  std::vector<const SinkLoadRegionSplitNode*> pending_nodes;
  pending_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    pending_nodes.push_back(&node);
  }
  while (!pending_nodes.empty()) {
    const auto* node = pending_nodes.back();
    pending_nodes.pop_back();
    if (node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null split node during leaf counting.");
    }
    if (node->children.empty()) {
      ++count;
    } else {
      for (const auto& child : node->children) {
        pending_nodes.push_back(&child);
      }
    }
  }
  return count;
}

auto CalcSplitDepth(const std::vector<SinkLoadRegionSplitNode>& nodes) -> unsigned
{
  unsigned depth = 0U;
  std::vector<std::pair<const SinkLoadRegionSplitNode*, unsigned>> pending_nodes;
  pending_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    pending_nodes.emplace_back(&node, 1U);
  }
  while (!pending_nodes.empty()) {
    const auto [node, node_depth] = pending_nodes.back();
    pending_nodes.pop_back();
    if (node == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null split node during depth calculation.");
    }
    depth = std::max(depth, node_depth);
    for (const auto& child : node->children) {
      pending_nodes.emplace_back(&child, node_depth + 1U);
    }
  }
  return depth;
}

}  // namespace

auto SplitSinkLoadRegionGroup(const std::vector<Pin*>& loads, std::size_t max_fanout) -> SinkLoadRegionSplitPlan
{
  SinkLoadRegionSplitPlan plan;
  if (max_fanout <= 1U || loads.size() <= max_fanout) {
    return plan;
  }

  std::vector<Pin*> ordered_loads;
  ordered_loads.reserve(loads.size());
  for (auto* pin : loads) {
    if (pin != nullptr) {
      ordered_loads.push_back(pin);
    }
  }
  if (ordered_loads.size() <= max_fanout) {
    return plan;
  }

  std::ranges::sort(ordered_loads, CanonicalLoadLess);
  auto root = BuildLocalSplitNode(std::move(ordered_loads), max_fanout);
  if (root.children.empty()) {
    return plan;
  }
  plan.children = std::move(root.children);
  plan.subgroups.reserve(plan.children.size());
  plan.centers.reserve(plan.children.size());
  for (const auto& child : plan.children) {
    plan.subgroups.push_back(child.loads);
    plan.centers.push_back(child.center);
  }
  plan.local_depth = CalcSplitDepth(plan.children);
  plan.buffer_count = CountSplitBuffers(plan.children);
  plan.leaf_group_count = CountSplitLeaves(plan.children);
  plan.feasible = true;
  return plan;
}

auto ResolveSinkLoadRegionLegality(const Tree& topology, PatternId topology_pattern_id, const TopologyPatternLibrary& topology_library,
                                   const BufferPatternLibrary& segment_pattern_library, SinkLoadRegionLegalityContext& legality_context)
    -> SinkLoadRegionLegalitySummary
{
  const auto topology_pattern = topology_library.materialize(topology_pattern_id);
  const auto signature = ResolveSinkLoadRegionLegalitySignature(topology_pattern, segment_pattern_library);
  if (signature.bottom_most_buffered_level <= legality_context.max_monotone_failed_level) {
    SinkLoadRegionLegalitySummary result;
    result.bottom_most_buffered_level = signature.bottom_most_buffered_level;
    result.segment_pattern_id = signature.segment_pattern_id;
    result.violation = SinkLoadRegionViolation::kFanout;
    std::ostringstream detail;
    detail << "monotone_pruned_by_bottom_most_buffered_level threshold=" << legality_context.max_monotone_failed_level
           << ", candidate_level=" << signature.bottom_most_buffered_level;
    result.failure_reason = detail.str();
    return result;
  }

  const auto cache_it = legality_context.result_by_signature.find(signature);
  if (cache_it != legality_context.result_by_signature.end()) {
    return cache_it->second;
  }

  auto evaluated = EvaluateSinkLoadRegionLegality(topology, signature, segment_pattern_library, legality_context);
  if (!evaluated.legal && evaluated.monotone_hard_fail && signature.bottom_most_buffered_level > legality_context.max_monotone_failed_level) {
    legality_context.max_monotone_failed_level = signature.bottom_most_buffered_level;
    legality_context.first_monotone_hard_fail_reason = evaluated.failure_reason;
    CTSLOG.warn(Loc::current(), "HTree: sink-load-region monotone threshold raised to bottom-most buffered level ", signature.bottom_most_buffered_level,
                " by hard violation: ", evaluated.failure_reason);
  }
  return legality_context.result_by_signature.emplace(signature, std::move(evaluated)).first->second;
}

auto FilterSinkLoadRegionLegalEntries(const std::vector<HTreeTopologyChar>& entries, const Tree& topology, const TopologyPatternLibrary& topology_library,
                                      const BufferPatternLibrary& segment_pattern_library, SinkLoadRegionLegalityContext& legality_context)
    -> SinkLoadRegionEntryFilterBuild
{
  SinkLoadRegionEntryFilterBuild result;
  result.output.entries.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto legality = ResolveSinkLoadRegionLegality(topology, entry.get_pattern_id(), topology_library, segment_pattern_library, legality_context);
    if (!legality.legal) {
      if (result.summary.first_failure_reason.empty()) {
        result.summary.first_failure_reason = legality.failure_reason;
      }
      continue;
    }
    result.summary.max_split_group_count = std::max(result.summary.max_split_group_count, legality.split_group_count);
    result.summary.max_split_extra_buffer_count = std::max(result.summary.max_split_extra_buffer_count, legality.split_extra_buffer_count);
    result.summary.max_split_local_depth = std::max(result.summary.max_split_local_depth, legality.split_local_depth);
    result.output.entries.push_back(entry);
  }
  return result;
}

}  // namespace icts::htree
