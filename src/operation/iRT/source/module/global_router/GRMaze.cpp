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
#include "GRMaze.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <utility>
#include <vector>

#include "GRCost.hpp"

namespace irt {

namespace {

bool hasCoord(const std::vector<PlanarCoord>& coord_list, const PlanarCoord& coord)
{
  return std::find(coord_list.begin(), coord_list.end(), coord) != coord_list.end();
}

int32_t getState(int32_t x, int32_t y, int32_t dir, int32_t x_size)
{
  return (y * x_size + x) * 2 + dir;
}

int32_t getStateX(int32_t state, int32_t x_size)
{
  return (state / 2) % x_size;
}

int32_t getStateY(int32_t state, int32_t x_size)
{
  return (state / 2) / x_size;
}

double getWireCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRMazeWorkspace& workspace, int32_t x, int32_t y, bool is_horizontal)
{
  int32_t state = getState(x, y, is_horizontal ? 0 : 1, workspace.x_size);
  if (workspace.edge_cost[state] == GRMazeWorkspace::kNoEdgeCost) {
    workspace.edge_cost[state] = wireMoveCost(grid, net, params, x, y, is_horizontal);
    workspace.cost_touched.push_back(state);
  }
  return workspace.edge_cost[state];
}

PlanarCoord selectNearestTerminal(const std::vector<PlanarCoord>& pending_terminal_list, const std::vector<PlanarCoord>& tree_coord_list)
{
  PlanarCoord target = pending_terminal_list.front();
  int32_t target_dist = INT_MAX;
  for (const PlanarCoord& terminal : pending_terminal_list) {
    for (const PlanarCoord& tree_coord : tree_coord_list) {
      int32_t dist = std::abs(terminal.get_x() - tree_coord.get_x()) + std::abs(terminal.get_y() - tree_coord.get_y());
      if (dist < target_dist) {
        target_dist = dist;
        target = terminal;
      }
    }
  }
  return target;
}

bool searchPath(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRMazeWorkspace& workspace, const PlanarCoord& target,
                const std::vector<PlanarCoord>& tree_coord_list, std::vector<PlanarCoord>& path)
{
  struct MazeQueueNode
  {
    double priority = 0.0;
    int32_t state = -1;

    bool operator<(const MazeQueueNode& other) const { return priority > other.priority; }
  };

  int32_t x_size = workspace.x_size;
  int32_t y_size = workspace.y_size;
  workspace.clearSearch();
  std::priority_queue<MazeQueueNode> queue;
  for (const PlanarCoord& tree_coord : tree_coord_list) {
    for (int32_t dir = 0; dir < 2; dir++) {
      int32_t state = getState(tree_coord.get_x(), tree_coord.get_y(), dir, x_size);
      workspace.setCost(state, 0.0, -1);
      double priority = std::abs(tree_coord.get_x() - target.get_x()) + std::abs(tree_coord.get_y() - target.get_y());
      queue.push(MazeQueueNode{priority, state});
    }
  }

  int32_t goal_state = -1;
  while (!queue.empty()) {
    MazeQueueNode current = queue.top();
    queue.pop();
    int32_t state = current.state;
    if (workspace.closed[state] != 0) {
      continue;
    }
    workspace.setClosed(state);
    int32_t x = getStateX(state, x_size);
    int32_t y = getStateY(state, x_size);
    int32_t dir = state % 2;
    if (x == target.get_x() && y == target.get_y()) {
      goal_state = state;
      break;
    }
    double current_cost = workspace.g[state];
    bool is_horizontal = (dir == 0);
    for (int32_t step : {1, -1}) {
      int32_t next_x = is_horizontal ? x + step : x;
      int32_t next_y = is_horizontal ? y : y + step;
      if (next_x < 0 || next_x >= x_size || next_y < 0 || next_y >= y_size) {
        continue;
      }
      int32_t edge_x = is_horizontal ? std::min(x, next_x) : x;
      int32_t edge_y = is_horizontal ? y : std::min(y, next_y);
      double wire_cost = getWireCost(net, grid, params, workspace, edge_x, edge_y, is_horizontal);
      if (wire_cost >= GRParams::kInf) {
        continue;
      }
      int32_t next_state = getState(next_x, next_y, dir, x_size);
      double next_cost = current_cost + wire_cost;
      if (next_cost < workspace.g[next_state]) {
        workspace.setCost(next_state, next_cost, state);
        double priority = next_cost + std::abs(next_x - target.get_x()) + std::abs(next_y - target.get_y());
        queue.push(MazeQueueNode{priority, next_state});
      }
    }
    int32_t turn_state = state ^ 1;
    double turn_cost = current_cost + directionCost(params);
    if (turn_cost < workspace.g[turn_state]) {
      workspace.setCost(turn_state, turn_cost, state);
      double priority = turn_cost + std::abs(x - target.get_x()) + std::abs(y - target.get_y());
      queue.push(MazeQueueNode{priority, turn_state});
    }
  }
  if (goal_state < 0) {
    return false;
  }

  std::vector<PlanarCoord> reverse_path;
  for (int32_t state = goal_state; state != -1; state = workspace.parent[state]) {
    reverse_path.emplace_back(getStateX(state, x_size), getStateY(state, x_size));
  }
  std::reverse(reverse_path.begin(), reverse_path.end());
  path.clear();
  for (const PlanarCoord& coord : reverse_path) {
    if (path.empty() || path.back() != coord) {
      path.push_back(coord);
    }
  }
  return path.size() >= 2;
}

}  // namespace

bool buildMazePathList(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRMazeWorkspace& workspace,
                       std::vector<std::vector<PlanarCoord>>& path_list)
{
  path_list.clear();
  if (net.get_pin_list().empty()) {
    return false;
  }
  const PlanarCoord& root_coord = net.get_pin_list().front().grid_coord;
  std::vector<PlanarCoord> tree_coord_list{root_coord};
  std::vector<PlanarCoord> pending_terminal_list;
  for (const GRPin& gr_pin : net.get_pin_list()) {
    const PlanarCoord& coord = gr_pin.grid_coord;
    if (coord == root_coord || hasCoord(pending_terminal_list, coord)) {
      continue;
    }
    pending_terminal_list.push_back(coord);
  }
  if (pending_terminal_list.empty()) {
    return true;
  }

  workspace.beginNet();
  while (!pending_terminal_list.empty()) {
    PlanarCoord target = selectNearestTerminal(pending_terminal_list, tree_coord_list);
    std::vector<PlanarCoord> path;
    if (!searchPath(net, grid, params, workspace, target, tree_coord_list, path)) {
      return false;
    }
    for (const PlanarCoord& coord : path) {
      if (!hasCoord(tree_coord_list, coord)) {
        tree_coord_list.push_back(coord);
      }
    }
    path_list.push_back(std::move(path));
    for (auto iter = pending_terminal_list.begin(); iter != pending_terminal_list.end();) {
      if (hasCoord(tree_coord_list, *iter)) {
        iter = pending_terminal_list.erase(iter);
      } else {
        ++iter;
      }
    }
  }
  return true;
}

void GRMazeWorkspace::init(int32_t x_size_, int32_t y_size_)
{
  if (x_size == x_size_ && y_size == y_size_) {
    return;
  }
  x_size = x_size_;
  y_size = y_size_;
  int32_t state_num = 2 * x_size * y_size;
  g.assign(state_num, GRParams::kInf);
  parent.assign(state_num, -1);
  closed.assign(state_num, 0);
  search_touched.clear();
  edge_cost.assign(state_num, kNoEdgeCost);
  cost_touched.clear();
}

void GRMazeWorkspace::beginNet()
{
  for (int32_t state : cost_touched) {
    edge_cost[state] = kNoEdgeCost;
  }
  cost_touched.clear();
}

void GRMazeWorkspace::clearSearch()
{
  for (int32_t state : search_touched) {
    g[state] = GRParams::kInf;
    parent[state] = -1;
    closed[state] = 0;
  }
  search_touched.clear();
}

void GRMazeWorkspace::setCost(int32_t state, double cost, int32_t parent_state)
{
  g[state] = cost;
  parent[state] = parent_state;
  search_touched.push_back(state);
}

void GRMazeWorkspace::setClosed(int32_t state)
{
  closed[state] = 1;
  search_touched.push_back(state);
}

}  // namespace irt
