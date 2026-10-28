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
#include "GRTreeBuilder.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include "GRCost.hpp"
#include "Logger.hpp"
#include "Net.hpp"
#include "flute3/flute.h"

namespace irt {

namespace {

struct SteinerNode
{
  PlanarCoord coord;
  int32_t fixed_low = INT_MAX;
  int32_t fixed_high = INT_MIN;
  int32_t parent = -1;
  std::vector<int32_t> child_list;
};

struct SteinerTree
{
  std::vector<SteinerNode> node_list;
  int32_t root_idx = -1;
};

struct FluteTreeGuard
{
  Flute::Tree tree{0, 0, nullptr};
  ~FluteTreeGuard() { Flute::free_tree(tree); }
};

bool attachPinLayerRange(const GRNet& net, const std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, SteinerTree& steiner_tree)
{
  for (const GRPin& gr_pin : net.get_pin_list()) {
    auto iter = coord_node_map.find(gr_pin.grid_coord);
    if (iter == coord_node_map.end()) {
      RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " pin coordinate is not in the FLUTE tree!");
      return false;
    }
    SteinerNode& node = steiner_tree.node_list[iter->second];
    node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
    node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
  }
  return true;
}

bool buildFluteSteinerTree(const GRNet& net, const std::vector<PlanarCoord>& coord_list, SteinerTree& steiner_tree)
{
  std::vector<Flute::DTYPE> x_list(coord_list.size());
  std::vector<Flute::DTYPE> y_list(coord_list.size());
  for (size_t i = 0; i < coord_list.size(); i++) {
    x_list[i] = coord_list[i].get_x();
    y_list[i] = coord_list[i].get_y();
  }
  FluteTreeGuard flute_tree_guard;
  flute_tree_guard.tree = Flute::flute(static_cast<int32_t>(coord_list.size()), x_list.data(), y_list.data(), FLUTE_ACCURACY);
  const Flute::Tree& flute_tree = flute_tree_guard.tree;
  if (flute_tree.deg <= 0) {
    RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " FLUTE tree is empty!");
    return false;
  }

  // branch: tree node, n: parent idx
  int32_t branch_num = 2 * flute_tree.deg - 2;
  std::vector<std::vector<int32_t>> neighbor_list(branch_num);
  for (int32_t branch_idx = 0; branch_idx < branch_num; branch_idx++) {
    int32_t neighbor_idx = flute_tree.branch[branch_idx].n;
    if (neighbor_idx < 0 || neighbor_idx >= branch_num || neighbor_idx == branch_idx) {
      continue;
    }
    neighbor_list[branch_idx].push_back(neighbor_idx);
    neighbor_list[neighbor_idx].push_back(branch_idx);
  }

  std::vector<int32_t> parent_branch_list(branch_num, -1);
  std::vector<int32_t> branch_order_list;
  std::queue<int32_t> branch_queue;
  parent_branch_list[0] = 0;
  branch_queue.push(0);
  // bfs from root to get a tree from pin node, not steiner node
  while (!branch_queue.empty()) {
    int32_t branch_idx = branch_queue.front();
    branch_queue.pop();
    branch_order_list.push_back(branch_idx);
    for (int32_t neighbor_idx : neighbor_list[branch_idx]) {
      if (parent_branch_list[neighbor_idx] == -1) {
        parent_branch_list[neighbor_idx] = branch_idx;
        branch_queue.push(neighbor_idx);
      }
    }
  }
  if (static_cast<int32_t>(branch_order_list.size()) != branch_num) {
    RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " FLUTE tree is not connected!");
    return false;
  }

  std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC> coord_node_map;
  for (int32_t branch_idx : branch_order_list) {
    PlanarCoord coord(flute_tree.branch[branch_idx].x, flute_tree.branch[branch_idx].y);
    int32_t node_idx = -1;
    auto iter = coord_node_map.find(coord);
    if (iter == coord_node_map.end()) {
      SteinerNode node;
      node.coord = coord;
      node_idx = static_cast<int32_t>(steiner_tree.node_list.size());
      steiner_tree.node_list.push_back(node);
      coord_node_map[coord] = node_idx;
    } else {
      node_idx = iter->second;
    }
    if (branch_idx == 0) {
      steiner_tree.root_idx = node_idx;
    }
  }
  for (size_t i = 1; i < branch_order_list.size(); i++) {
    int32_t child_branch_idx = branch_order_list[i];
    int32_t parent_branch_idx = parent_branch_list[child_branch_idx];
    PlanarCoord parent_coord(flute_tree.branch[parent_branch_idx].x, flute_tree.branch[parent_branch_idx].y);
    PlanarCoord child_coord(flute_tree.branch[child_branch_idx].x, flute_tree.branch[child_branch_idx].y);
    if (parent_coord == child_coord) {
      continue;
    }
    int32_t parent_node_idx = coord_node_map[parent_coord];
    int32_t child_node_idx = coord_node_map[child_coord];
    if (steiner_tree.node_list[child_node_idx].parent != -1) {
      continue;
    }
    steiner_tree.node_list[child_node_idx].parent = parent_node_idx;
    steiner_tree.node_list[parent_node_idx].child_list.push_back(child_node_idx);
  }

  if (!attachPinLayerRange(net, coord_node_map, steiner_tree)) {
    return false;
  }
  if (steiner_tree.root_idx < 0) {
    return false;
  }
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(steiner_tree.node_list.size()); node_idx++) {
    if (node_idx != steiner_tree.root_idx && steiner_tree.node_list[node_idx].parent == -1) {
      return false;
    }
  }
  return true;
}

bool buildSteinerTree(const GRNet& net, SteinerTree& steiner_tree)
{
  steiner_tree = SteinerTree();
  std::vector<PlanarCoord> coord_list;
  coord_list.reserve(net.get_pin_list().size());
  for (const GRPin& gr_pin : net.get_pin_list()) {
    coord_list.push_back(gr_pin.grid_coord);
  }
  std::sort(coord_list.begin(), coord_list.end(), CmpPlanarCoordByXASC());
  coord_list.erase(std::unique(coord_list.begin(), coord_list.end()), coord_list.end());
  if (coord_list.empty()) {
    RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " has no unique pin coordinate!");
    return false;
  }

  if (coord_list.size() == 1) {
    SteinerNode root_node;
    root_node.coord = coord_list.front();
    steiner_tree.node_list.push_back(root_node);
    steiner_tree.root_idx = 0;
    for (const GRPin& gr_pin : net.get_pin_list()) {
      SteinerNode& node = steiner_tree.node_list[0];
      node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
      node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
    }
    return true;
  }
  return buildFluteSteinerTree(net, coord_list, steiner_tree);
}

struct PathNode
{
  PlanarCoord coord;
  int32_t fixed_low = INT_MAX;
  int32_t fixed_high = INT_MIN;
  int32_t parent = -1;
  bool is_bend = false;
  std::vector<int32_t> child_list;
  std::vector<std::vector<int32_t>> child_bend_list;  // parallel to child_list, stores the first node of each candidate pattern chain
};

struct PathTree
{
  std::vector<PathNode> node_list;
  int32_t root_idx = -1;
};

int32_t getOrCreatePathNode(PathTree& path_tree, std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, const PlanarCoord& coord)
{
  auto iter = coord_node_map.find(coord);
  if (iter != coord_node_map.end()) {
    return iter->second;
  }
  PathNode node;
  node.coord = coord;
  int32_t node_idx = static_cast<int32_t>(path_tree.node_list.size());
  path_tree.node_list.push_back(node);
  coord_node_map[coord] = node_idx;
  return node_idx;
}

int32_t addBendNode(PathTree& path_tree, int32_t parent_idx, int32_t child_idx, const PlanarCoord& coord)
{
  PathNode bend_node;
  bend_node.coord = coord;
  bend_node.parent = parent_idx;
  bend_node.is_bend = true;
  bend_node.child_list.push_back(child_idx);
  bend_node.child_bend_list.emplace_back();
  int32_t bend_idx = static_cast<int32_t>(path_tree.node_list.size());
  path_tree.node_list.push_back(std::move(bend_node));
  return bend_idx;
}

// add bend node from candidate path
int32_t addPatternPath(PathTree& path_tree, int32_t parent_idx, int32_t child_idx, const std::vector<PlanarCoord>& path)
{
  int32_t next_idx = child_idx;
  for (int32_t i = static_cast<int32_t>(path.size()) - 1; i >= 0; i--) {
    next_idx = addBendNode(path_tree, parent_idx, next_idx, path[i]);
  }
  return next_idx;
}

// connect path node
void addPathEdge(PathTree& path_tree, int32_t parent_idx, int32_t child_idx, std::vector<int32_t> bend_idx_list = {})
{
  if (parent_idx == child_idx) {
    return;
  }
  PathNode& child_node = path_tree.node_list[child_idx];
  if (child_node.parent != -1) {
    return;
  }
  child_node.parent = parent_idx;
  PathNode& parent_node = path_tree.node_list[parent_idx];
  parent_node.child_list.push_back(child_idx);
  parent_node.child_bend_list.push_back(std::move(bend_idx_list));
}

void addPathCandidate(const GRGridGraph& grid, std::vector<std::vector<PlanarCoord>>& path_list, const std::vector<PlanarCoord>& path)
{
  for (const PlanarCoord& coord : path) {
    if (coord.get_x() < 0 || coord.get_x() >= grid.getXSize() || coord.get_y() < 0 || coord.get_y() >= grid.getYSize()) {
      return;
    }
  }
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i - 1] == path[i]) {
      return;
    }
  }
  if (std::find(path_list.begin(), path_list.end(), path) != path_list.end()) {
    return;
  }
  path_list.push_back(path);
}

void buildPatternPathList(const GRGridGraph& grid, const PlanarCoord& parent_coord, const PlanarCoord& child_coord,
                          std::vector<std::vector<PlanarCoord>>& path_list)
{
  path_list.clear();
  bool is_collinear = parent_coord.get_x() == child_coord.get_x() || parent_coord.get_y() == child_coord.get_y();
  if (is_collinear) {
    return;
  }
  // L-pattern
  addPathCandidate(grid, path_list, {PlanarCoord(child_coord.get_x(), parent_coord.get_y())});
  addPathCandidate(grid, path_list, {PlanarCoord(parent_coord.get_x(), child_coord.get_y())});

  // Z-pattern
  constexpr double kFractionList[] = {1.0 / 8.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 2.0, 5.0 / 8.0, 3.0 / 4.0, 7.0 / 8.0};
  std::set<int32_t> used_x;
  for (double fraction : kFractionList) {
    int32_t x = parent_coord.get_x() + static_cast<int32_t>(std::lround((child_coord.get_x() - parent_coord.get_x()) * fraction));
    if (x < 0 || x >= grid.getXSize() || x == parent_coord.get_x() || x == child_coord.get_x()) {
      continue;
    }
    if (!used_x.insert(x).second) {
      continue;
    }
    addPathCandidate(grid, path_list, {PlanarCoord(x, parent_coord.get_y()), PlanarCoord(x, child_coord.get_y())});
  }

  std::set<int32_t> used_y;
  for (double fraction : kFractionList) {
    int32_t y = parent_coord.get_y() + static_cast<int32_t>(std::lround((child_coord.get_y() - parent_coord.get_y()) * fraction));
    if (y < 0 || y >= grid.getYSize() || y == parent_coord.get_y() || y == child_coord.get_y()) {
      continue;
    }
    if (!used_y.insert(y).second) {
      continue;
    }
    addPathCandidate(grid, path_list, {PlanarCoord(parent_coord.get_x(), y), PlanarCoord(child_coord.get_x(), y)});
  }
}

void addUDetourPathList(const GRGridGraph& grid, const GRParams& params, const PlanarCoord& parent_coord, const PlanarCoord& child_coord,
                        std::vector<std::vector<PlanarCoord>>& path_list)
{
  int32_t x_min = std::min(parent_coord.get_x(), child_coord.get_x());
  int32_t x_max = std::max(parent_coord.get_x(), child_coord.get_x());
  int32_t y_min = std::min(parent_coord.get_y(), child_coord.get_y());
  int32_t y_max = std::max(parent_coord.get_y(), child_coord.get_y());

  if (parent_coord.get_y() != child_coord.get_y()) {
    for (int32_t offset = 1; offset <= params.max_detour_offset; offset++) {
      int32_t left_x = x_min - offset;
      if (left_x >= 0) {
        addPathCandidate(grid, path_list, {PlanarCoord(left_x, parent_coord.get_y()), PlanarCoord(left_x, child_coord.get_y())});
      }
      int32_t right_x = x_max + offset;
      if (right_x < grid.getXSize()) {
        addPathCandidate(grid, path_list, {PlanarCoord(right_x, parent_coord.get_y()), PlanarCoord(right_x, child_coord.get_y())});
      }
    }
  }
  if (parent_coord.get_x() != child_coord.get_x()) {
    for (int32_t offset = 1; offset <= params.max_detour_offset; offset++) {
      int32_t lower_y = y_min - offset;
      if (lower_y >= 0) {
        addPathCandidate(grid, path_list, {PlanarCoord(parent_coord.get_x(), lower_y), PlanarCoord(child_coord.get_x(), lower_y)});
      }
      int32_t upper_y = y_max + offset;
      if (upper_y < grid.getYSize()) {
        addPathCandidate(grid, path_list, {PlanarCoord(parent_coord.get_x(), upper_y), PlanarCoord(child_coord.get_x(), upper_y)});
      }
    }
  }
}

void buildDetourPathList(const GRGridGraph& grid, const GRParams& params, const GRDetourGrid& detour_grid, const PlanarCoord& parent_coord,
                         const PlanarCoord& child_coord, std::vector<std::vector<PlanarCoord>>& path_list)
{
  buildPatternPathList(grid, parent_coord, child_coord, path_list);
  bool is_collinear = parent_coord.get_x() == child_coord.get_x() || parent_coord.get_y() == child_coord.get_y();
  if (is_collinear) {
    for (int32_t offset = 1; offset <= params.max_detour_offset; offset++) {
      for (int32_t sign : {1, -1}) {
        if (parent_coord.get_y() == child_coord.get_y()) {
          int32_t y = parent_coord.get_y() + sign * offset;
          if (y < 0 || y >= grid.getYSize()) {
            continue;
          }
          addPathCandidate(grid, path_list, {PlanarCoord(parent_coord.get_x(), y), PlanarCoord(child_coord.get_x(), y)});
        } else {
          int32_t x = parent_coord.get_x() + sign * offset;
          if (x < 0 || x >= grid.getXSize()) {
            continue;
          }
          addPathCandidate(grid, path_list, {PlanarCoord(x, parent_coord.get_y()), PlanarCoord(x, child_coord.get_y())});
        }
      }
    }
  } else if (detour_grid.getSegmentCongestionNum(parent_coord, child_coord) > 0) {
    addUDetourPathList(grid, params, parent_coord, child_coord, path_list);
  }
}

bool attachPinLayerRange(const GRNet& net, const std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, PathTree& path_tree)
{
  for (const GRPin& gr_pin : net.get_pin_list()) {
    auto iter = coord_node_map.find(gr_pin.grid_coord);
    if (iter == coord_node_map.end()) {
      return false;
    }
    PathNode& node = path_tree.node_list[iter->second];
    node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
    node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
  }
  return true;
}

bool buildPathTreeFromPaths(const GRNet& net, const std::vector<std::vector<PlanarCoord>>& path_list, PathTree& path_tree)
{
  path_tree = PathTree();
  std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC> coord_node_map;
  std::vector<std::set<int32_t>> adjacency;
  // build DAG
  for (const std::vector<PlanarCoord>& path : path_list) {
    int32_t prev_idx = -1;
    for (const PlanarCoord& coord : path) {
      int32_t node_idx = getOrCreatePathNode(path_tree, coord_node_map, coord);
      if (adjacency.size() < path_tree.node_list.size()) {
        adjacency.resize(path_tree.node_list.size());
      }
      if (prev_idx >= 0 && prev_idx != node_idx) {
        adjacency[prev_idx].insert(node_idx);
        adjacency[node_idx].insert(prev_idx);
      }
      prev_idx = node_idx;
    }
  }
  if (path_tree.node_list.empty()) {
    if (net.get_pin_list().empty()) {
      return false;
    }
    getOrCreatePathNode(path_tree, coord_node_map, net.get_pin_list().front().grid_coord);
    adjacency.resize(path_tree.node_list.size());
  }

  // get root and build tree
  auto root_iter = coord_node_map.find(net.get_pin_list().front().grid_coord);
  if (root_iter == coord_node_map.end()) {
    return false;
  }
  int32_t root_idx = root_iter->second;
  path_tree.root_idx = root_idx;
  int32_t node_num = static_cast<int32_t>(path_tree.node_list.size());
  std::vector<int32_t> parent(node_num, -1);
  std::queue<int32_t> queue;
  parent[root_idx] = root_idx;
  queue.push(root_idx);
  int32_t visited_num = 0;
  while (!queue.empty()) {
    int32_t node_idx = queue.front();
    queue.pop();
    visited_num++;
    for (int32_t neighbor_idx : adjacency[node_idx]) {
      if (parent[neighbor_idx] != -1) {
        continue;
      }
      parent[neighbor_idx] = node_idx;
      addPathEdge(path_tree, node_idx, neighbor_idx);
      queue.push(neighbor_idx);
    }
  }
  if (visited_num != node_num) {
    return false;
  }
  return attachPinLayerRange(net, coord_node_map, path_tree);
}

bool buildPathTree(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const SteinerTree& steiner_tree, const GRDetourGrid* detour_grid,
                   PathTree& path_tree)
{
  path_tree = PathTree();
  if (steiner_tree.node_list.empty() || steiner_tree.root_idx < 0) {
    return false;
  }
  for (const SteinerNode& steiner_node : steiner_tree.node_list) {
    PathNode path_node;
    path_node.coord = steiner_node.coord;
    path_node.fixed_low = steiner_node.fixed_low;
    path_node.fixed_high = steiner_node.fixed_high;
    path_tree.node_list.push_back(path_node);
  }
  path_tree.root_idx = steiner_tree.root_idx;

  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(steiner_tree.node_list.size()); node_idx++) {
    if (node_idx == steiner_tree.root_idx) {
      continue;
    }
    int32_t parent_idx = steiner_tree.node_list[node_idx].parent;
    if (parent_idx < 0) {
      return false;
    }
    const PlanarCoord& parent_coord = steiner_tree.node_list[parent_idx].coord;
    const PlanarCoord& child_coord = steiner_tree.node_list[node_idx].coord;
    std::vector<std::vector<PlanarCoord>> path_list;
    if (detour_grid == nullptr) {
      buildPatternPathList(grid, parent_coord, child_coord, path_list);
    } else {
      buildDetourPathList(grid, params, *detour_grid, parent_coord, child_coord, path_list);
    }

    // child bend list
    std::vector<int32_t> candidate_list;
    candidate_list.reserve(path_list.size() + 1);
    for (const std::vector<PlanarCoord>& path : path_list) {
      candidate_list.push_back(addPatternPath(path_tree, parent_idx, node_idx, path));
    }
    bool is_collinear = parent_coord.get_x() == child_coord.get_x() || parent_coord.get_y() == child_coord.get_y();
    if (is_collinear) {
      candidate_list.insert(candidate_list.begin(), -1);
    }
    if (candidate_list.empty()) {
      RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " has no pattern candidate!");
      return false;
    }
    addPathEdge(path_tree, parent_idx, node_idx, std::move(candidate_list));
  }
  return true;
}

struct DpState
{
  struct NodeWorkspace
  {
    std::vector<double> via_cost;
    std::vector<double> phi;              // via_cost 前缀和
    std::vector<int32_t> blocked_prefix;  // 不可用层数前缀和
    std::vector<double> child_g;          // [child_pos][layer]
    std::vector<int32_t> child_bend;      // [child_pos][layer] 取到该代价的折线链首节点
    std::vector<double> child_range_min;  // [child_pos][begin][end]
    std::vector<int32_t> child_range_argmin;
    std::vector<double> sum_min;  // [begin][end]

    void reset(int32_t child_num, int32_t layer_num)
    {
      via_cost.assign(layer_num, 0.0);
      phi.assign(layer_num + 1, 0.0);
      blocked_prefix.assign(layer_num + 1, 0);
      child_g.assign(child_num * layer_num, GRParams::kInf);
      child_bend.assign(child_num * layer_num, -1);
      child_range_min.assign(child_num * layer_num * layer_num, GRParams::kInf);
      child_range_argmin.assign(child_num * layer_num * layer_num, -1);
      sum_min.assign(layer_num * layer_num, GRParams::kInf);
    }
  };

  const PathTree& path_tree;
  int32_t layer_num;
  int32_t bottom_layer_idx;
  int32_t top_layer_idx;
  std::vector<std::vector<double>> d;
  std::vector<std::vector<int32_t>> best_b;
  std::vector<std::vector<int32_t>> best_t;
  std::vector<std::vector<std::vector<int32_t>>> best_child_layer;
  std::vector<std::vector<std::vector<int32_t>>> best_child_bend;
  std::vector<bool> computed;
  NodeWorkspace work;

  DpState(const PathTree& tree, int32_t node_num, int32_t layer_num_, int32_t bottom_, int32_t top_)
      : path_tree(tree),
        layer_num(layer_num_),
        bottom_layer_idx(bottom_),
        top_layer_idx(top_),
        d(node_num, std::vector<double>(layer_num_, GRParams::kInf)),
        best_b(node_num, std::vector<int32_t>(layer_num_, -1)),
        best_t(node_num, std::vector<int32_t>(layer_num_, -1)),
        best_child_layer(node_num, std::vector<std::vector<int32_t>>(layer_num_)),
        best_child_bend(node_num, std::vector<std::vector<int32_t>>(layer_num_)),
        computed(node_num, false)
  {
  }

  int32_t childOffset(int32_t child_pos) const { return child_pos * layer_num; }
  int32_t rangeOffset(int32_t child_pos, int32_t begin_layer, int32_t end_layer) const { return (child_pos * layer_num + begin_layer) * layer_num + end_layer; }
  int32_t sumOffset(int32_t begin_layer, int32_t end_layer) const { return begin_layer * layer_num + end_layer; }
};

void getPostorder(int32_t node_idx, const PathTree& path_tree, std::vector<int32_t>& postorder)
{
  for (int32_t child_idx : path_tree.node_list[node_idx].child_list) {
    getPostorder(child_idx, path_tree, postorder);
  }
  postorder.push_back(node_idx);
}

void prepareViaCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, DpState& state, int32_t node_idx)
{
  const PlanarCoord& coord = state.path_tree.node_list[node_idx].coord;
  DpState::NodeWorkspace& work = state.work;
  for (int32_t layer_idx = state.bottom_layer_idx; layer_idx < state.top_layer_idx; layer_idx++) {
    work.via_cost[layer_idx] = viaCost(grid, net, params, coord, layer_idx);
  }
  for (int32_t layer_idx = 0; layer_idx < state.layer_num; layer_idx++) {
    if (work.via_cost[layer_idx] >= GRParams::kInf) {
      work.phi[layer_idx + 1] = work.phi[layer_idx];
      work.blocked_prefix[layer_idx + 1] = work.blocked_prefix[layer_idx] + 1;
    } else {
      work.phi[layer_idx + 1] = work.phi[layer_idx] + work.via_cost[layer_idx];
      work.blocked_prefix[layer_idx + 1] = work.blocked_prefix[layer_idx];
    }
  }
}

void computeChildCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, DpState& state, int32_t node_idx)
{
  const PathNode& node = state.path_tree.node_list[node_idx];
  DpState::NodeWorkspace& work = state.work;
  for (int32_t child_pos = 0; child_pos < static_cast<int32_t>(node.child_list.size()); child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    const PathNode& child_node = state.path_tree.node_list[child_idx];
    const std::vector<int32_t>& bend_idx_list = node.child_bend_list[child_pos];
    int32_t child_offset = state.childOffset(child_pos);
    if (bend_idx_list.empty()) {
      for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
        double wire_cost = segmentCost(grid, net, params, layer_idx, node.coord, child_node.coord);
        work.child_g[child_offset + layer_idx] = addCost(state.d[child_idx][layer_idx], wire_cost);
      }
      continue;
    }
    for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
      double best_cost = GRParams::kInf;
      int32_t best_bend_idx = -1;
      for (int32_t bend_idx : bend_idx_list) {
        double cost = GRParams::kInf;
        if (bend_idx < 0) {
          double wire_cost = segmentCost(grid, net, params, layer_idx, node.coord, child_node.coord);
          cost = addCost(state.d[child_idx][layer_idx], wire_cost);
        } else {
          const PlanarCoord& bend_coord = state.path_tree.node_list[bend_idx].coord;
          double wire_cost = segmentCost(grid, net, params, layer_idx, node.coord, bend_coord);
          cost = addCost(state.d[bend_idx][layer_idx], wire_cost);
        }
        if (cost < best_cost) {
          best_cost = cost;
          best_bend_idx = bend_idx;
        }
      }
      work.child_g[child_offset + layer_idx] = best_cost;
      work.child_bend[child_offset + layer_idx] = best_bend_idx;
    }
  }
}

void buildChildRangeMin(DpState& state, int32_t child_num)
{
  DpState::NodeWorkspace& work = state.work;
  for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
    int32_t child_offset = state.childOffset(child_pos);
    for (int32_t begin_layer = 0; begin_layer < state.layer_num; begin_layer++) {
      double min_cost = GRParams::kInf;
      int32_t min_layer = -1;
      for (int32_t end_layer = begin_layer; end_layer < state.layer_num; end_layer++) {
        if (work.child_g[child_offset + end_layer] < min_cost) {
          min_cost = work.child_g[child_offset + end_layer];
          min_layer = end_layer;
        }
        int32_t range_offset = state.rangeOffset(child_pos, begin_layer, end_layer);
        work.child_range_min[range_offset] = min_cost;
        work.child_range_argmin[range_offset] = min_layer;
      }
    }
  }
}

void sumChildRangeMin(DpState& state, int32_t child_num)
{
  DpState::NodeWorkspace& work = state.work;
  for (int32_t begin_layer = state.bottom_layer_idx; begin_layer <= state.top_layer_idx; begin_layer++) {
    for (int32_t end_layer = begin_layer; end_layer <= state.top_layer_idx; end_layer++) {
      double sum = 0.0;
      bool is_valid = true;
      for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
        double child_min = work.child_range_min[state.rangeOffset(child_pos, begin_layer, end_layer)];
        if (child_min >= GRParams::kInf) {
          is_valid = false;
          break;
        }
        sum += child_min;
      }
      if (is_valid) {
        work.sum_min[state.sumOffset(begin_layer, end_layer)] = sum;
      }
    }
  }
}

void selectLayerWindow(DpState& state, int32_t node_idx)
{
  const PathNode& node = state.path_tree.node_list[node_idx];
  DpState::NodeWorkspace& work = state.work;
  int32_t child_num = static_cast<int32_t>(node.child_list.size());
  for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
    int32_t begin_limit = node.fixed_low == INT_MAX ? layer_idx : std::min(node.fixed_low, layer_idx);
    int32_t end_start = node.fixed_high == INT_MIN ? layer_idx : std::max(node.fixed_high, layer_idx);
    begin_limit = std::min(begin_limit, state.top_layer_idx);
    end_start = std::max(end_start, state.bottom_layer_idx);

    double best_cost = GRParams::kInf;
    int32_t best_begin = -1;
    int32_t best_end = -1;
    for (int32_t begin_layer = state.bottom_layer_idx; begin_layer <= begin_limit; begin_layer++) {
      for (int32_t end_layer = end_start; end_layer <= state.top_layer_idx; end_layer++) {
        if (work.blocked_prefix[end_layer] != work.blocked_prefix[begin_layer]) {
          continue;
        }
        double stack_cost = work.phi[end_layer] - work.phi[begin_layer];
        double total_cost = addCost(stack_cost, work.sum_min[state.sumOffset(begin_layer, end_layer)]);
        if (total_cost < best_cost) {
          best_cost = total_cost;
          best_begin = begin_layer;
          best_end = end_layer;
        }
      }
    }
    if (best_begin < 0) {
      continue;
    }
    state.d[node_idx][layer_idx] = best_cost;
    state.best_b[node_idx][layer_idx] = best_begin;
    state.best_t[node_idx][layer_idx] = best_end;
    state.best_child_layer[node_idx][layer_idx].assign(child_num, -1);
    state.best_child_bend[node_idx][layer_idx].assign(child_num, -1);
    for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
      int32_t best_child_layer_idx = work.child_range_argmin[state.rangeOffset(child_pos, best_begin, best_end)];
      state.best_child_layer[node_idx][layer_idx][child_pos] = best_child_layer_idx;
      if (best_child_layer_idx >= 0) {
        state.best_child_bend[node_idx][layer_idx][child_pos] = work.child_bend[state.childOffset(child_pos) + best_child_layer_idx];
      }
    }
  }
}

void computeNodeDP(const GRNet& net, const GRGridGraph& grid, const GRParams& params, DpState& state, int32_t node_idx)
{
  if (state.computed[node_idx]) {
    return;
  }
  const PathNode& node = state.path_tree.node_list[node_idx];
  int32_t child_num = static_cast<int32_t>(node.child_list.size());

  // 临时空间只有一份, 必须先把子节点递归算完
  for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    if (!state.computed[child_idx]) {
      computeNodeDP(net, grid, params, state, child_idx);
    }
    for (int32_t bend_idx : node.child_bend_list[child_pos]) {
      if (bend_idx >= 0 && !state.computed[bend_idx]) {
        computeNodeDP(net, grid, params, state, bend_idx);
      }
    }
  }

  state.work.reset(child_num, state.layer_num);
  prepareViaCost(net, grid, params, state, node_idx);
  computeChildCost(net, grid, params, state, node_idx);
  buildChildRangeMin(state, child_num);
  sumChildRangeMin(state, child_num);
  selectLayerWindow(state, node_idx);
  state.computed[node_idx] = true;
}

void reconstructTree(const DpState& state, int32_t node_idx, int32_t layer_idx, GRTree& routing_tree)
{
  const PathTree& path_tree = state.path_tree;
  const PathNode& node = path_tree.node_list[node_idx];
  int32_t begin_layer = state.best_b[node_idx][layer_idx];
  int32_t end_layer = state.best_t[node_idx][layer_idx];
  if (begin_layer < 0 || end_layer < 0) {
    return;
  }
  if (begin_layer < end_layer) {
    routing_tree.via_list.push_back(GRViaStack{node.coord, begin_layer, end_layer});
  }
  for (size_t child_pos = 0; child_pos < node.child_list.size(); child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    int32_t child_layer_idx = state.best_child_layer[node_idx][layer_idx][child_pos];
    if (child_layer_idx < 0) {
      continue;
    }
    int32_t bend_idx = state.best_child_bend[node_idx][layer_idx][child_pos];
    int32_t next_idx = bend_idx < 0 ? child_idx : bend_idx;
    const PathNode& next_node = path_tree.node_list[next_idx];
    LayerCoord first_coord(node.coord, child_layer_idx);
    LayerCoord second_coord(next_node.coord, child_layer_idx);
    if (first_coord != second_coord) {
      routing_tree.wire_list.push_back(GRWireSegment{first_coord, second_coord});
    }
    reconstructTree(state, next_idx, child_layer_idx, routing_tree);
  }
}

// 层分配 DP: 后序求每个节点在各层的子树最优代价
bool assignLayers(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const PathTree& path_tree, GRTree& routing_tree)
{
  int32_t node_num = static_cast<int32_t>(path_tree.node_list.size());
  int32_t layer_num = grid.getLayerNum();
  int32_t bottom_layer_idx = grid.getBottomLayerIdx();
  int32_t top_layer_idx = grid.getTopLayerIdx();
  if (node_num == 0 || path_tree.root_idx < 0) {
    return false;
  }

  int32_t logical_node_num = 0;
  for (const PathNode& node : path_tree.node_list) {
    if (!node.is_bend) {
      logical_node_num++;
    }
  }

  std::vector<int32_t> postorder;
  getPostorder(path_tree.root_idx, path_tree, postorder);
  if (postorder.size() != static_cast<size_t>(logical_node_num)) {
    RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " path tree is disconnected!");
    return false;
  }

  DpState state(path_tree, node_num, layer_num, bottom_layer_idx, top_layer_idx);
  for (int32_t node_idx : postorder) {
    computeNodeDP(net, grid, params, state, node_idx);
  }

  int32_t root_layer_idx = -1;
  double root_cost = GRParams::kInf;
  for (int32_t layer_idx = bottom_layer_idx; layer_idx <= top_layer_idx; layer_idx++) {
    if (state.d[path_tree.root_idx][layer_idx] < root_cost) {
      root_cost = state.d[path_tree.root_idx][layer_idx];
      root_layer_idx = layer_idx;
    }
  }
  if (root_layer_idx < 0) {
    return false;
  }
  reconstructTree(state, path_tree.root_idx, root_layer_idx, routing_tree);
  return true;
}

struct DetourChain
{
  std::vector<int32_t> node_idx_list;
  std::vector<PlanarCoord> coord_list;
  bool is_horizontal = false;
  int32_t congestion_num = 0;
  double max_length = 0.0;
};

double getPathLength(const std::vector<PlanarCoord>& path)
{
  double length = 0.0;
  for (size_t i = 1; i < path.size(); i++) {
    length += std::abs(path[i].get_x() - path[i - 1].get_x()) + std::abs(path[i].get_y() - path[i - 1].get_y());
  }
  return length;
}

bool isStraightSteinerChain(const SteinerTree& steiner_tree, const std::vector<int32_t>& chain)
{
  if (chain.size() < 2) {
    return false;
  }
  const PlanarCoord& first_coord = steiner_tree.node_list[chain[0]].coord;
  const PlanarCoord& second_coord = steiner_tree.node_list[chain[1]].coord;
  bool is_horizontal = first_coord.get_y() == second_coord.get_y();
  int32_t direction = 0;
  for (size_t i = 1; i < chain.size(); i++) {
    const PlanarCoord& prev_coord = steiner_tree.node_list[chain[i - 1]].coord;
    const PlanarCoord& curr_coord = steiner_tree.node_list[chain[i]].coord;
    if (is_horizontal) {
      if (prev_coord.get_y() != curr_coord.get_y() || prev_coord.get_x() == curr_coord.get_x()) {
        return false;
      }
      int32_t sign = curr_coord.get_x() > prev_coord.get_x() ? 1 : -1;
      if (direction != 0 && direction != sign) {
        return false;
      }
      direction = sign;
    } else {
      if (prev_coord.get_x() != curr_coord.get_x() || prev_coord.get_y() == curr_coord.get_y()) {
        return false;
      }
      int32_t sign = curr_coord.get_y() > prev_coord.get_y() ? 1 : -1;
      if (direction != 0 && direction != sign) {
        return false;
      }
      direction = sign;
    }
  }
  return true;
}

int32_t getCongestionPrefixSum(const GridMap<int32_t>& prefix_map, int32_t begin_x, int32_t begin_y, int32_t end_x, int32_t end_y)
{
  if (begin_x < 0 || begin_y < 0 || end_x < begin_x || end_y < begin_y || end_x + 1 >= prefix_map.get_x_size() || end_y + 1 >= prefix_map.get_y_size()) {
    return 0;
  }
  return prefix_map[end_x + 1][end_y + 1] - prefix_map[begin_x][end_y + 1] - prefix_map[end_x + 1][begin_y] + prefix_map[begin_x][begin_y];
}

double evaluateShift(const GRDetourGrid& detour_grid, const std::set<PlanarCoord, CmpPlanarCoordByXASC>& coord_set, const DetourChain& chain, int32_t offset)
{
  std::vector<PlanarCoord> shifted_coord_list = chain.coord_list;
  std::set<PlanarCoord, CmpPlanarCoordByXASC> shifted_coord_set;
  for (size_t i = 1; i + 1 < shifted_coord_list.size(); i++) {
    PlanarCoord shifted_coord = shifted_coord_list[i];
    if (chain.is_horizontal) {
      shifted_coord.set_y(shifted_coord.get_y() + offset);
    } else {
      shifted_coord.set_x(shifted_coord.get_x() + offset);
    }
    if (shifted_coord.get_x() < 0 || shifted_coord.get_x() >= detour_grid.x_size || shifted_coord.get_y() < 0 || shifted_coord.get_y() >= detour_grid.y_size) {
      return GRParams::kInf;
    }
    if (coord_set.find(shifted_coord) != coord_set.end() || !shifted_coord_set.insert(shifted_coord).second) {
      return GRParams::kInf;
    }
    shifted_coord_list[i] = shifted_coord;
  }
  int32_t shifted_congestion = detour_grid.getPathCongestionNum(shifted_coord_list);
  if (shifted_congestion >= chain.congestion_num) {
    return GRParams::kInf;
  }
  double shifted_length = getPathLength(shifted_coord_list);
  if (shifted_length > chain.max_length + GRParams::kEpsilon) {
    return GRParams::kInf;
  }
  return shifted_congestion * 1000000.0 + shifted_length;
}

void collectDetourChains(const GRParams& params, const SteinerTree& steiner_tree, const GRDetourGrid& detour_grid, std::vector<DetourChain>& chain_list)
{
  int32_t node_num = static_cast<int32_t>(steiner_tree.node_list.size());
  std::vector<std::vector<int32_t>> adjacency(node_num);
  for (int32_t node_idx = 0; node_idx < node_num; node_idx++) {
    for (int32_t child_idx : steiner_tree.node_list[node_idx].child_list) {
      adjacency[node_idx].push_back(child_idx);
      adjacency[child_idx].push_back(node_idx);
    }
  }

  for (int32_t begin_idx = 0; begin_idx < node_num; begin_idx++) {
    for (int32_t end_idx = begin_idx + 1; end_idx < node_num; end_idx++) {
      std::vector<int32_t> parent_list(node_num, -1);
      std::queue<int32_t> node_queue;
      parent_list[begin_idx] = begin_idx;
      node_queue.push(begin_idx);
      while (!node_queue.empty() && parent_list[end_idx] == -1) {
        int32_t node_idx = node_queue.front();
        node_queue.pop();
        for (int32_t neighbor_idx : adjacency[node_idx]) {
          if (parent_list[neighbor_idx] != -1) {
            continue;
          }
          parent_list[neighbor_idx] = node_idx;
          node_queue.push(neighbor_idx);
        }
      }
      if (parent_list[end_idx] == -1) {
        continue;
      }
      std::vector<int32_t> chain;
      for (int32_t node_idx = end_idx; node_idx != begin_idx; node_idx = parent_list[node_idx]) {
        chain.push_back(node_idx);
      }
      chain.push_back(begin_idx);
      std::reverse(chain.begin(), chain.end());
      if (chain.size() < 3 || !isStraightSteinerChain(steiner_tree, chain)) {
        continue;
      }
      // 中间节点必须是虚拟节点, 否则平移会带走 pin
      bool has_fixed_node = false;
      for (size_t i = 1; i + 1 < chain.size(); i++) {
        if (steiner_tree.node_list[chain[i]].fixed_low != INT_MAX) {
          has_fixed_node = true;
          break;
        }
      }
      if (has_fixed_node) {
        continue;
      }

      DetourChain detour_chain;
      detour_chain.node_idx_list = chain;
      detour_chain.coord_list.reserve(chain.size());
      for (int32_t node_idx : chain) {
        detour_chain.coord_list.push_back(steiner_tree.node_list[node_idx].coord);
      }
      detour_chain.congestion_num = detour_grid.getPathCongestionNum(detour_chain.coord_list);
      if (detour_chain.congestion_num <= 0) {
        continue;
      }
      double original_length = getPathLength(detour_chain.coord_list);
      detour_chain.max_length = original_length + std::max(2.0, original_length * params.max_detour_length_ratio);
      detour_chain.is_horizontal = detour_chain.coord_list.front().get_y() == detour_chain.coord_list[1].get_y();
      chain_list.push_back(std::move(detour_chain));
    }
  }
}

void applyShift(const DetourChain& chain, int32_t offset, SteinerTree& detour_steiner_tree)
{
  for (size_t i = 1; i + 1 < chain.node_idx_list.size(); i++) {
    SteinerNode& node = detour_steiner_tree.node_list[chain.node_idx_list[i]];
    if (chain.is_horizontal) {
      node.coord.set_y(node.coord.get_y() + offset);
    } else {
      node.coord.set_x(node.coord.get_x() + offset);
    }
  }
}

// detour: 把压过拥堵的直线骨架段整体侧移
bool shiftSteinerTree(const SteinerTree& steiner_tree, const GRDetourGrid& detour_grid, const GRParams& params, SteinerTree& detour_steiner_tree)
{
  detour_steiner_tree = steiner_tree;
  if (steiner_tree.node_list.size() < 3) {
    return false;
  }
  std::set<PlanarCoord, CmpPlanarCoordByXASC> coord_set;
  for (const SteinerNode& node : steiner_tree.node_list) {
    coord_set.insert(node.coord);
  }

  // 分数按 (拥堵数, 长度) 字典序
  std::vector<DetourChain> chain_list;
  collectDetourChains(params, steiner_tree, detour_grid, chain_list);
  const DetourChain* best_chain = nullptr;
  int32_t best_offset = 0;
  double best_score = GRParams::kInf;
  for (const DetourChain& chain : chain_list) {
    for (int32_t offset = -params.max_detour_offset; offset <= params.max_detour_offset; offset++) {
      if (offset == 0) {
        continue;
      }
      double score = evaluateShift(detour_grid, coord_set, chain, offset);
      if (score < best_score) {
        best_score = score;
        best_chain = &chain;
        best_offset = offset;
      }
    }
  }
  if (best_chain == nullptr) {
    return false;
  }
  applyShift(*best_chain, best_offset, detour_steiner_tree);
  return true;
}

void addDirectConnectVia(GRTree& routing_tree, const PlanarCoord& coord, int32_t first_layer_idx, int32_t second_layer_idx)
{
  if (first_layer_idx == second_layer_idx) {
    return;
  }
  routing_tree.via_list.push_back(GRViaStack{coord, std::min(first_layer_idx, second_layer_idx), std::max(first_layer_idx, second_layer_idx)});
}

}  // namespace

bool buildPatternTree(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRTree& routing_tree)
{
  routing_tree = GRTree();
  SteinerTree steiner_tree;
  if (!buildSteinerTree(net, steiner_tree)) {
    return false;
  }
  PathTree path_tree;
  if (!buildPathTree(net, grid, params, steiner_tree, nullptr, path_tree)) {
    return false;
  }
  return assignLayers(net, grid, params, path_tree, routing_tree);
}

bool buildDetourTree(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const GRDetourGrid& detour_grid, GRTree& routing_tree,
                     bool& detour_shifted)
{
  detour_shifted = false;
  routing_tree = GRTree();
  SteinerTree steiner_tree;
  if (!buildSteinerTree(net, steiner_tree)) {
    return false;
  }
  SteinerTree detour_steiner_tree;
  detour_shifted = shiftSteinerTree(steiner_tree, detour_grid, params, detour_steiner_tree);
  PathTree path_tree;
  if (!buildPathTree(net, grid, params, detour_steiner_tree, &detour_grid, path_tree)) {
    return false;
  }
  return assignLayers(net, grid, params, path_tree, routing_tree);
}

bool buildTreeFromPaths(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const std::vector<std::vector<PlanarCoord>>& path_list,
                        GRTree& routing_tree)
{
  routing_tree = GRTree();
  PathTree path_tree;
  if (!buildPathTreeFromPaths(net, path_list, path_tree)) {
    return false;
  }
  return assignLayers(net, grid, params, path_tree, routing_tree);
}

void buildDirectTree(const GRNet& net, const GRGridGraph& grid, GRTree& routing_tree)
{
  routing_tree = GRTree();
  if (net.get_pin_list().empty()) {
    return;
  }
  int32_t horizontal_layer_idx = -1;
  int32_t vertical_layer_idx = -1;
  for (int32_t layer_idx = grid.getBottomLayerIdx(); layer_idx <= grid.getTopLayerIdx(); layer_idx++) {
    if (grid.isHorizontalLayer(layer_idx)) {
      if (horizontal_layer_idx == -1) {
        horizontal_layer_idx = layer_idx;
      }
    } else if (vertical_layer_idx == -1) {
      vertical_layer_idx = layer_idx;
    }
    if (horizontal_layer_idx != -1 && vertical_layer_idx != -1) {
      break;
    }
  }
  if (horizontal_layer_idx == -1 || vertical_layer_idx == -1) {
    return;
  }

  const GRPin& root_pin = net.get_pin_list().front();
  for (size_t pin_offset = 1; pin_offset < net.get_pin_list().size(); pin_offset++) {
    const GRPin& gr_pin = net.get_pin_list()[pin_offset];
    // 先水平再垂直, 最多两条直线
    PlanarCoord corner(gr_pin.grid_coord.get_x(), root_pin.grid_coord.get_y());
    bool has_horizontal_wire = (root_pin.grid_coord != corner);
    bool has_vertical_wire = (corner != gr_pin.grid_coord);
    int32_t root_wire_layer_idx = has_horizontal_wire ? horizontal_layer_idx : vertical_layer_idx;
    int32_t pin_wire_layer_idx = has_vertical_wire ? vertical_layer_idx : horizontal_layer_idx;
    if (!has_horizontal_wire && !has_vertical_wire) {
      addDirectConnectVia(routing_tree, root_pin.grid_coord, root_pin.layer_idx, gr_pin.layer_idx);
      continue;
    }
    if (has_horizontal_wire) {
      routing_tree.wire_list.push_back(GRWireSegment{LayerCoord(root_pin.grid_coord, horizontal_layer_idx), LayerCoord(corner, horizontal_layer_idx)});
    }
    if (has_vertical_wire) {
      routing_tree.wire_list.push_back(GRWireSegment{LayerCoord(corner, vertical_layer_idx), LayerCoord(gr_pin.grid_coord, vertical_layer_idx)});
    }
    if (has_horizontal_wire && has_vertical_wire) {
      addDirectConnectVia(routing_tree, corner, horizontal_layer_idx, vertical_layer_idx);
    }
    addDirectConnectVia(routing_tree, root_pin.grid_coord, root_pin.layer_idx, root_wire_layer_idx);
    addDirectConnectVia(routing_tree, gr_pin.grid_coord, gr_pin.layer_idx, pin_wire_layer_idx);
  }
}

GRDetourGrid GRDetourGrid::build(const GRGridGraph& grid)
{
  GRDetourGrid detour_grid;
  detour_grid.x_size = grid.getXSize();
  detour_grid.y_size = grid.getYSize();
  int32_t x_size = detour_grid.x_size;
  int32_t y_size = detour_grid.y_size;
  GridMap<int32_t> horizontal_map(x_size, y_size, 0);
  GridMap<int32_t> vertical_map(x_size, y_size, 0);
  for (int32_t layer_idx = grid.getBottomLayerIdx(); layer_idx <= grid.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = grid.getEdgeMap(layer_idx);
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        if (edge_map[x][y].getOverflow() <= GRParams::kEpsilon) {
          continue;
        }
        if (grid.isHorizontalLayer(layer_idx)) {
          horizontal_map[x][y] = 1;
        } else {
          vertical_map[x][y] = 1;
        }
      }
    }
  }

  detour_grid.horizontal_prefix.init(x_size + 1, y_size + 1, 0);
  detour_grid.vertical_prefix.init(x_size + 1, y_size + 1, 0);
  for (int32_t x = 0; x < x_size; x++) {
    for (int32_t y = 0; y < y_size; y++) {
      detour_grid.horizontal_prefix[x + 1][y + 1]
          = horizontal_map[x][y] + detour_grid.horizontal_prefix[x][y + 1] + detour_grid.horizontal_prefix[x + 1][y] - detour_grid.horizontal_prefix[x][y];
      detour_grid.vertical_prefix[x + 1][y + 1]
          = vertical_map[x][y] + detour_grid.vertical_prefix[x][y + 1] + detour_grid.vertical_prefix[x + 1][y] - detour_grid.vertical_prefix[x][y];
    }
  }
  return detour_grid;
}

int32_t GRDetourGrid::getSegmentCongestionNum(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const
{
  if (first_coord == second_coord) {
    return 0;
  }
  if (first_coord.get_y() == second_coord.get_y()) {
    int32_t begin_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t end_x = std::max(first_coord.get_x(), second_coord.get_x()) - 1;
    if (end_x < begin_x) {
      return 0;
    }
    return getCongestionPrefixSum(horizontal_prefix, begin_x, first_coord.get_y(), end_x, first_coord.get_y());
  }
  if (first_coord.get_x() == second_coord.get_x()) {
    int32_t begin_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t end_y = std::max(first_coord.get_y(), second_coord.get_y()) - 1;
    if (end_y < begin_y) {
      return 0;
    }
    return getCongestionPrefixSum(vertical_prefix, first_coord.get_x(), begin_y, first_coord.get_x(), end_y);
  }
  PlanarCoord first_mid(first_coord.get_x(), second_coord.get_y());
  PlanarCoord second_mid(second_coord.get_x(), first_coord.get_y());
  int32_t first_num = getSegmentCongestionNum(first_coord, first_mid) + getSegmentCongestionNum(first_mid, second_coord);
  int32_t second_num = getSegmentCongestionNum(first_coord, second_mid) + getSegmentCongestionNum(second_mid, second_coord);
  return std::min(first_num, second_num);
}

int32_t GRDetourGrid::getPathCongestionNum(const std::vector<PlanarCoord>& path) const
{
  int32_t congestion_num = 0;
  for (size_t i = 1; i < path.size(); i++) {
    congestion_num += getSegmentCongestionNum(path[i - 1], path[i]);
  }
  return congestion_num;
}

}  // namespace irt
