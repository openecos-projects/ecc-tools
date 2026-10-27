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
#include "Logger.hpp"

namespace irt {

namespace {

bool hasCoord(const std::vector<PlanarCoord>& coord_list, const PlanarCoord& coord)
{
  return std::find(coord_list.begin(), coord_list.end(), coord) != coord_list.end();
}

}  // namespace

bool GRMaze::buildPathList(const GRNet& net, const GRGridGraph& grid, const GRParams& params, std::vector<std::vector<PlanarCoord>>& path_list)
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

  int32_t x_size = grid.getXSize();
  int32_t y_size = grid.getYSize();
  Field& field = field_;
  field.init(x_size, y_size);
  field.beginNet();

  struct MazeQueueNode
  {
    double priority = 0.0;
    int32_t state = -1;

    bool operator<(const MazeQueueNode& other) const { return priority > other.priority; }
  };

  while (!pending_terminal_list.empty()) {
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

    field.clearSearch();
    std::priority_queue<MazeQueueNode> queue;
    for (const PlanarCoord& tree_coord : tree_coord_list) {
      for (int32_t dir = 0; dir < 2; dir++) {
        int32_t state = (tree_coord.get_y() * x_size + tree_coord.get_x()) * 2 + dir;
        field.setCost(state, 0.0, -1);
        double priority = std::abs(tree_coord.get_x() - target.get_x()) + std::abs(tree_coord.get_y() - target.get_y());
        queue.push(MazeQueueNode{priority, state});
      }
    }

    int32_t goal_state = -1;
    while (!queue.empty()) {
      MazeQueueNode current = queue.top();
      queue.pop();
      int32_t state = current.state;
      if (field.closed[state] != 0) {
        continue;
      }
      field.setClosed(state);
      int32_t base = state / 2;
      int32_t x = base % x_size;
      int32_t y = base / x_size;
      int32_t dir = state % 2;
      if (x == target.get_x() && y == target.get_y()) {
        goal_state = state;
        break;
      }
      double current_cost = field.g[state];
      if (dir == 0) {
        if (x + 1 < x_size) {
          double wire_cost = getWireCost(net, grid, params, x, y, true);
          if (wire_cost < GRParams::kInf) {
            int32_t next_state = (y * x_size + x + 1) * 2;
            double next_cost = current_cost + wire_cost;
            if (next_cost < field.g[next_state]) {
              field.setCost(next_state, next_cost, state);
              double priority = next_cost + std::abs(x + 1 - target.get_x()) + std::abs(y - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
        if (x > 0) {
          double wire_cost = getWireCost(net, grid, params, x - 1, y, true);
          if (wire_cost < GRParams::kInf) {
            int32_t next_state = (y * x_size + x - 1) * 2;
            double next_cost = current_cost + wire_cost;
            if (next_cost < field.g[next_state]) {
              field.setCost(next_state, next_cost, state);
              double priority = next_cost + std::abs(x - 1 - target.get_x()) + std::abs(y - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
      } else {
        if (y + 1 < y_size) {
          double wire_cost = getWireCost(net, grid, params, x, y, false);
          if (wire_cost < GRParams::kInf) {
            int32_t next_state = ((y + 1) * x_size + x) * 2 + 1;
            double next_cost = current_cost + wire_cost;
            if (next_cost < field.g[next_state]) {
              field.setCost(next_state, next_cost, state);
              double priority = next_cost + std::abs(x - target.get_x()) + std::abs(y + 1 - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
        if (y > 0) {
          double wire_cost = getWireCost(net, grid, params, x, y - 1, false);
          if (wire_cost < GRParams::kInf) {
            int32_t next_state = ((y - 1) * x_size + x) * 2 + 1;
            double next_cost = current_cost + wire_cost;
            if (next_cost < field.g[next_state]) {
              field.setCost(next_state, next_cost, state);
              double priority = next_cost + std::abs(x - target.get_x()) + std::abs(y - 1 - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
      }
      int32_t next_state = state ^ 1;
      double next_cost = current_cost + directionCost(params);
      if (next_cost < field.g[next_state]) {
        field.setCost(next_state, next_cost, state);
        double priority = next_cost + std::abs(x - target.get_x()) + std::abs(y - target.get_y());
        queue.push(MazeQueueNode{priority, next_state});
      }
    }
    if (goal_state < 0) {
      return false;
    }

    std::vector<PlanarCoord> reverse_path;
    int32_t state = goal_state;
    while (state != -1) {
      int32_t base = state / 2;
      reverse_path.emplace_back(base % x_size, base / x_size);
      state = field.parent[state];
    }
    std::reverse(reverse_path.begin(), reverse_path.end());
    std::vector<PlanarCoord> path;
    for (const PlanarCoord& coord : reverse_path) {
      if (path.empty() || path.back() != coord) {
        path.push_back(coord);
      }
    }
    if (path.size() < 2) {
      return false;
    }
    path_list.push_back(std::move(path));
    for (const PlanarCoord& coord : path_list.back()) {
      if (!hasCoord(tree_coord_list, coord)) {
        tree_coord_list.push_back(coord);
      }
    }
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

double GRMaze::getWireCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, int32_t x, int32_t y, bool is_horizontal)
{
  int32_t state = (y * grid.getXSize() + x) * 2 + (is_horizontal ? 0 : 1);
  if (field_.edge_cost[state] == kNoEdgeCost) {
    field_.edge_cost[state] = computeWireCost(net, grid, params, x, y, is_horizontal);
    field_.cost_touched.push_back(state);
  }
  return field_.edge_cost[state];
}

double GRMaze::computeWireCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, int32_t x, int32_t y, bool is_horizontal) const
{
  double total_supply = 0.0;
  double total_demand = 0.0;
  double total_congestion_cost = 0.0;
  int32_t congestion_layer_num = 0;
  bool has_edge = false;
  for (int32_t layer_idx = grid.getBottomLayerIdx(); layer_idx <= grid.getTopLayerIdx(); layer_idx++) {
    if (grid.isHorizontalLayer(layer_idx) != is_horizontal) {
      continue;
    }
    GRWireEdgeId edge_id{x, y, layer_idx};
    if (!grid.hasWireEdge(edge_id)) {
      continue;
    }
    if (net.isExempt(edge_id)) {
      return params.unit_wire_cost;
    }
    const GRWireEdge& edge = grid.getWireEdge(edge_id);
    has_edge = true;
    total_supply += edge.getUsableSupply();
    total_demand += edge.getDemand();
    total_congestion_cost += edge.congestion_cost;
    congestion_layer_num++;
  }
  if (!has_edge || total_supply <= GRParams::kEpsilon) {
    return GRParams::kInf;
  }
  double average_congestion_cost = congestion_layer_num == 0 ? 0.0 : total_congestion_cost / congestion_layer_num;
  return params.unit_wire_cost + params.cost_weight * congestionCost(params, total_supply, total_demand, 1.0) + average_congestion_cost;
}

void GRMaze::Field::init(int32_t x_size_, int32_t y_size_)
{
  if (x_size == x_size_ && y_size == y_size_) {
    return;
  }
  x_size = x_size_;
  y_size = y_size_;
  int32_t state_num = 2 * x_size * y_size;
  g.assign(state_num, GRParams::kInf);
  parent.assign(state_num, -1);
  closed.assign(state_num, false);
  search_touched.clear();
  edge_cost.assign(state_num, kNoEdgeCost);
  cost_touched.clear();
}

void GRMaze::Field::beginNet()
{
  for (int32_t state : cost_touched) {
    edge_cost[state] = kNoEdgeCost;
  }
  cost_touched.clear();
}

void GRMaze::Field::clearSearch()
{
  for (int32_t state : search_touched) {
    g[state] = GRParams::kInf;
    parent[state] = -1;
    closed[state] = false;
  }
  search_touched.clear();
}

void GRMaze::Field::setCost(int32_t state, double cost, int32_t parent_state)
{
  g[state] = cost;
  parent[state] = parent_state;
  search_touched.push_back(state);
}

void GRMaze::Field::setClosed(int32_t state)
{
  closed[state] = 1;
  search_touched.push_back(state);
}

}  // namespace irt
