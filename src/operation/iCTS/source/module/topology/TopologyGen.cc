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
 * @file TopologyGen.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Topology generator for CTS.
 */

#include "TopologyGen.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "Geometry.hh"
#include "Logger.hh"
#include "Pin.hh"
#include "Point.hh"
#include "Tree.hh"
#include "clustering/Clustering.hh"
#include "config/TopologyConfig.hh"

namespace icts {
namespace {

struct LoadBounds
{
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
};

auto CalcLoadBounds(const std::vector<Pin*>& loads) -> LoadBounds
{
  LoadBounds bounds;
  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    bounds.min_x = std::min(bounds.min_x, loc.get_x());
    bounds.min_y = std::min(bounds.min_y, loc.get_y());
    bounds.max_x = std::max(bounds.max_x, loc.get_x());
    bounds.max_y = std::max(bounds.max_y, loc.get_y());
  }
  if (loads.empty()) {
    bounds.min_x = 0;
    bounds.min_y = 0;
    bounds.max_x = 0;
    bounds.max_y = 0;
  }
  return bounds;
}

auto ResolveMaxNodeLoadCount(std::size_t leaf_need, const BiPartitionConfig& config) -> std::size_t
{
  if (config.max_leaf_load_count == 0U || leaf_need == 0U) {
    return 0U;
  }

  return leaf_need * config.max_leaf_load_count;
}

auto ResolveRequestedBranchingFactor(const TopologyGenConfig& config) -> std::size_t
{
  if (config.branching_factor > 0U) {
    return std::max<std::size_t>(2U, config.branching_factor);
  }
  return TopologyGen::resolveBranchingFactor(config.partition_config.max_leaf_load_count);
}

struct ChildPartitionFrame
{
  std::vector<Pin*> loads;
  std::size_t first_child = 0U;
  std::size_t child_count = 0U;
  std::size_t leaf_need_per_child = 0U;
};

auto CalcClusterCenter(const std::vector<Pin*>& loads) -> Point<int>
{
  if (loads.empty()) {
    return Point<int>(0, 0);
  }
  const auto center = geometry::CalcCenter(loads, [](Pin* pin) -> auto { return pin->get_location(); });
  return Point<int>(static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y())));
}

auto BuildKWayChildPartitions(const std::vector<Pin*>& loads, std::size_t child_count, std::size_t leaf_need_per_child, const BiPartitionConfig& config)
    -> ClusterOutput
{
  ClusterOutput output;
  if (child_count == 0U) {
    return output;
  }

  output.clusters.resize(child_count);
  std::vector<ChildPartitionFrame> stack;
  stack.push_back(ChildPartitionFrame{
      .loads = loads,
      .first_child = 0U,
      .child_count = child_count,
      .leaf_need_per_child = leaf_need_per_child,
  });

  while (!stack.empty()) {
    auto frame = std::move(stack.back());
    stack.pop_back();
    if (frame.child_count <= 1U || frame.loads.size() <= 1U) {
      output.clusters.at(frame.first_child) = std::move(frame.loads);
      continue;
    }

    const std::size_t left_child_count = frame.child_count / 2U;
    const std::size_t right_child_count = frame.child_count - left_child_count;
    const std::size_t min_leaf_need = std::max<std::size_t>(1U, std::min(left_child_count, right_child_count) * frame.leaf_need_per_child);
    const std::size_t max_leaf_need = std::max(left_child_count, right_child_count) * frame.leaf_need_per_child;
    auto partition_config = config;
    partition_config.max_cluster_size = ResolveMaxNodeLoadCount(max_leaf_need, config);
    auto split = Clustering::biPartition(frame.loads, min_leaf_need, partition_config);
    if (split.clusters.size() < 2U) {
      output.clusters.at(frame.first_child) = std::move(frame.loads);
      continue;
    }

    stack.push_back(ChildPartitionFrame{
        .loads = std::move(split.clusters.at(1)),
        .first_child = frame.first_child + left_child_count,
        .child_count = right_child_count,
        .leaf_need_per_child = frame.leaf_need_per_child,
    });
    stack.push_back(ChildPartitionFrame{
        .loads = std::move(split.clusters.at(0)),
        .first_child = frame.first_child,
        .child_count = left_child_count,
        .leaf_need_per_child = frame.leaf_need_per_child,
    });
  }

  output.centers.reserve(child_count);
  for (const auto& cluster : output.clusters) {
    output.centers.push_back(CalcClusterCenter(cluster));
  }
  return output;
}

}  // namespace

auto TopologyGen::build(const std::vector<Pin*>& loads, const Input& input, const Config& config) -> Tree
{
  return buildWithConfig(loads, input, config);
}

auto TopologyGen::resolveBranchingFactor(std::size_t max_leaf_load_count) -> std::size_t
{
  constexpr std::size_t binary_minimum = 2U;
  constexpr std::size_t two_dimensional_quadrants = 4U;
  if (max_leaf_load_count == 0U) {
    return binary_minimum;
  }
  return std::clamp(max_leaf_load_count, binary_minimum, two_dimensional_quadrants);
}

auto TopologyGen::buildWithConfig(const std::vector<Pin*>& loads, const Input& input, const Config& config) -> Tree
{
  Tree tree;
  if (loads.empty()) {
    CTSLOG.warn(Loc::current(), "Topology generation skipped: no loads.");
    return tree;
  }

  const std::size_t branching_factor = ResolveRequestedBranchingFactor(config);
  std::size_t leaf_count = calcLeafCount(loads.size(), branching_factor);
  const unsigned max_depth = calcMaxDepth(loads.size(), branching_factor);
  unsigned height = max_depth;
  if (config.target_depth.has_value()) {
    height = std::min(*config.target_depth, max_depth);
    leaf_count = 1U;
    for (unsigned level = 0U; level < height; ++level) {
      if (leaf_count > std::numeric_limits<std::size_t>::max() / branching_factor) {
        leaf_count = std::numeric_limits<std::size_t>::max();
        break;
      }
      leaf_count *= branching_factor;
    }
  }
  if (leaf_count == 0) {
    CTSLOG.warn(Loc::current(), "Topology generation skipped: leaf count is zero.");
    return tree;
  }

  const auto bounds = CalcLoadBounds(loads);

  const auto root = tree.create_node();
  tree.set_root(root);
  tree.get_node(root)->get_position() = input.fixed_root_location.value_or(geometry::CalcMedian(loads, [](Pin* pin) -> auto { return pin->get_location(); }));

  buildFullTree(tree, BuildCursor{.node_id = root, .depth = 0}, static_cast<int>(height), branching_factor);
  embedPositions(tree, root, loads, leaf_count, config.partition_config, branching_factor);
  balanceTopology(tree, bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y, config.partition_config.htree_topology_tolerance);
  return tree;
}

auto TopologyGen::calcMaxDepth(std::size_t load_count, std::size_t branching_factor) -> unsigned
{
  unsigned depth = 0U;
  branching_factor = std::max<std::size_t>(2U, branching_factor);
  for (std::size_t leaf_count = calcLeafCount(load_count, branching_factor); leaf_count > 1U; leaf_count /= branching_factor) {
    ++depth;
  }
  return depth;
}

auto TopologyGen::calcLeafCount(std::size_t load_count, std::size_t branching_factor) -> std::size_t
{
  if (load_count == 0) {
    return 0;
  }
  branching_factor = std::max<std::size_t>(2U, branching_factor);
  std::size_t leaf_count = 1;
  while (leaf_count <= load_count / branching_factor) {
    leaf_count *= branching_factor;
  }
  return leaf_count;
}

auto TopologyGen::buildFullTree(Tree& tree, const BuildCursor& cursor, int height, std::size_t branching_factor) -> void
{
  branching_factor = std::max<std::size_t>(2U, branching_factor);
  std::vector<BuildCursor> build_stack;
  build_stack.push_back(cursor);

  while (!build_stack.empty()) {
    auto current = build_stack.back();
    build_stack.pop_back();

    if (current.depth >= height) {
      continue;
    }

    for (std::size_t child_index = 0U; child_index < branching_factor; ++child_index) {
      const auto child = tree.add_child(current.node_id, child_index);
      build_stack.push_back(BuildCursor{.node_id = child, .depth = current.depth + 1});
    }
  }
}

auto TopologyGen::embedPositions(Tree& tree, std::size_t node, const std::vector<Pin*>& loads, std::size_t leaf_need, const BiPartitionConfig& config,
                                 std::size_t branching_factor) -> void
{
  struct EmbedFrame
  {
    std::size_t node_id = 0;
    std::vector<Pin*> node_loads;
    std::size_t node_leaf_need = 0;
  };

  std::vector<EmbedFrame> embed_stack;
  embed_stack.push_back(EmbedFrame{.node_id = node, .node_loads = loads, .node_leaf_need = leaf_need});

  while (!embed_stack.empty()) {
    auto frame = std::move(embed_stack.back());
    embed_stack.pop_back();

    auto* node_ptr = tree.get_node(frame.node_id);
    if (node_ptr == nullptr) {
      continue;
    }
    node_ptr->get_loads() = frame.node_loads;
    if (frame.node_loads.empty()) {
      continue;
    }

    if (node_ptr->isLeaf() || frame.node_leaf_need <= 1 || frame.node_loads.size() <= 1) {
      const auto center = geometry::CalcCenter(frame.node_loads, [](Pin* pin) -> auto { return pin->get_location(); });
      node_ptr->get_position() = Point<int>(static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y())));
      continue;
    }

    const auto& children = node_ptr->get_children();
    if (children.empty()) {
      continue;
    }
    const std::size_t child_count = std::min(children.size(), branching_factor);
    if (child_count == 0U) {
      continue;
    }

    const std::size_t child_leaf_need = std::max<std::size_t>(1U, frame.node_leaf_need / child_count);
    auto result = BuildKWayChildPartitions(frame.node_loads, child_count, child_leaf_need, config);
    if (result.clusters.empty()) {
      continue;
    }

    for (std::size_t child_index = child_count; child_index > 0U; --child_index) {
      const auto resolved_child_index = child_index - 1U;
      if (children.at(resolved_child_index) == std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      auto* child = tree.get_node(children.at(resolved_child_index));
      if (child == nullptr) {
        continue;
      }
      if (resolved_child_index < result.centers.size()) {
        child->get_position() = result.centers.at(resolved_child_index);
      }
      std::vector<Pin*> child_loads;
      if (resolved_child_index < result.clusters.size()) {
        child_loads = std::move(result.clusters.at(resolved_child_index));
      }
      embed_stack.push_back(EmbedFrame{
          .node_id = children.at(resolved_child_index),
          .node_loads = std::move(child_loads),
          .node_leaf_need = child_leaf_need,
      });
    }
  }
}

auto TopologyGen::balanceTopology(Tree& tree, int min_x, int min_y, int max_x, int max_y, double topology_tolerance) -> void
{
  auto levels = tree.levels();
  if (levels.size() <= 1) {
    return;
  }

  topology_tolerance = std::max(0.0, topology_tolerance);

  for (std::size_t level = 1; level < levels.size(); ++level) {
    int sum_dist = 0;
    std::size_t count = 0;
    for (const auto node_id : levels.at(level)) {
      auto* node = tree.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      auto* parent = tree.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }
      sum_dist += geometry::Manhattan(node->get_position(), parent->get_position());
      ++count;
    }
    if (count == 0) {
      continue;
    }
    const double avg_dist = static_cast<double>(sum_dist) / static_cast<double>(count);
    const double min_allowed_dist = avg_dist * std::max(0.0, 1.0 - topology_tolerance);
    const double max_allowed_dist = avg_dist * (1.0 + topology_tolerance);
    for (const auto node_id : levels.at(level)) {
      auto* node = tree.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      auto* parent = tree.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }
      const auto current_dist = static_cast<double>(geometry::Manhattan(node->get_position(), parent->get_position()));
      if (current_dist >= min_allowed_dist && current_dist <= max_allowed_dist) {
        continue;
      }
      const double target_dist = current_dist < min_allowed_dist ? min_allowed_dist : max_allowed_dist;
      node->get_position() = geometry::ProjectToL1Circle(parent->get_position(), node->get_position(), target_dist, min_x, min_y, max_x, max_y);
    }
  }
}

}  // namespace icts
