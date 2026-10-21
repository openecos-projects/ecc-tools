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
#include "GlobalRouter.hpp"

#include <cmath>
#include <queue>

#include "DataManager.hpp"
#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "RTHeader.hpp"
#include "RoutingLayer.hpp"
#include "Utility.hpp"
#include "flute3/flute.h"

namespace irt {

// public

GlobalRouter* GlobalRouter::_gr_instance = nullptr;

void GlobalRouter::initInst()
{
  if (_gr_instance == nullptr) {
    _gr_instance = new GlobalRouter();
  }
}

GlobalRouter& GlobalRouter::getInst()
{
  if (_gr_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_gr_instance;
}

void GlobalRouter::destroyInst()
{
  if (_gr_instance != nullptr) {
    delete _gr_instance;
    _gr_instance = nullptr;
  }
}

// function

void GlobalRouter::route()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  RTDM.getDatabase().get_net_global_result_map().clear();
  initGRGridGraph();
  analyzeSupply();
  initGRNetList();
  routeGRNetList();
  mazeRouteGRNetList();
  for (GRNet& gr_net : _gr_net_list) {
    outputNetResult(gr_net);
  }
  RTDM.rebuildGlobalResultRTree();
  // debugPlotGRModel("after_route");
  printSummary();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

void GlobalRouter::initGRGridGraph()
{
  _grid_graph.build(RTDM.getDatabase(), RTDM.getConfig().bottom_routing_layer_idx, RTDM.getConfig().top_routing_layer_idx);
  _short_cost_list.assign(_grid_graph.getLayerNum(), kDefaultShortCost);
}

void GlobalRouter::analyzeSupply()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    Monitor layer_monitor;
    analyzeSupplyLayer(layer_idx);
    RTLOG.info(Loc::current(), "Analyzed layer ", layer_idx, layer_monitor.getStatsInfo());
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void GlobalRouter::analyzeSupplyLayer(int32_t layer_idx)
{
  RoutingLayer& routing_layer = RTDM.getDatabase().get_routing_layer_list()[layer_idx];
  bool is_horizontal = routing_layer.isPreferH();
  int32_t x_size = is_horizontal ? std::max(0, _grid_graph.getXSize() - 1) : _grid_graph.getXSize();
  int32_t y_size = is_horizontal ? _grid_graph.getYSize() : std::max(0, _grid_graph.getYSize() - 1);

#pragma omp parallel for collapse(2)
  for (int32_t x = 0; x < x_size; x++) {
    for (int32_t y = 0; y < y_size; y++) {
      LayerCoord first_coord(x, y, layer_idx);
      LayerCoord second_coord = is_horizontal ? LayerCoord(x + 1, y, layer_idx) : LayerCoord(x, y + 1, layer_idx);
      EXTLayerRect search_rect = getSearchRect(first_coord, second_coord);
      std::vector<LayerRect> wire_list = getCrossingWireList(search_rect);
      std::vector<PlanarRect> enlarged_obs_rect_list;
      for (auto& [net_idx, fixed_rect_set] : RTDM.getNetFixedRectMap(true, search_rect)) {
        for (EXTLayerRect* fixed_rect : fixed_rect_set) {
          PlanarRect fixed_shape = fixed_rect->get_real_rect();
          enlarged_obs_rect_list.push_back(RTUTIL.getEnlargedRect(fixed_shape, routing_layer.getPRLSpacing(fixed_shape)));
        }
      }
      int32_t accessible_num = 0;
      for (LayerRect& wire : wire_list) {
        if (isAccess(wire, enlarged_obs_rect_list)) {
          accessible_num++;
        }
      }
      // reserve one wire track, keep at least zero
      int32_t usable_num = std::max(0, accessible_num - 1);
      GRWireEdge& wire_edge = _grid_graph.getWireEdge(GRWireEdgeId{x, y, layer_idx});
      wire_edge.supply = static_cast<double>(wire_list.size());
      wire_edge.fixed_demand = static_cast<double>(static_cast<int32_t>(wire_list.size()) - usable_num);
    }
  }
}

EXTLayerRect GlobalRouter::getSearchRect(const LayerCoord& first_coord, const LayerCoord& second_coord)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
    RTLOG.error(Loc::current(), "The grid_pair layer_idx is not equal!");
  }
  PlanarRect search_real_rect;
  {
    PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
    PlanarCoord first_mid_coord = first_real_rect.getMidPoint();
    PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
    PlanarCoord second_mid_coord = second_real_rect.getMidPoint();
    if (RTUTIL.isHorizontal(first_coord, second_coord)) {
      std::vector<PlanarCoord> coord_list;
      coord_list.emplace_back(first_mid_coord.get_x(), first_real_rect.get_ll_y());
      coord_list.emplace_back(first_mid_coord.get_x(), first_real_rect.get_ur_y());
      coord_list.emplace_back(second_mid_coord.get_x(), second_real_rect.get_ll_y());
      coord_list.emplace_back(second_mid_coord.get_x(), second_real_rect.get_ur_y());
      search_real_rect = RTUTIL.getBoundingBox(coord_list);
    } else if (RTUTIL.isVertical(first_coord, second_coord)) {
      std::vector<PlanarCoord> coord_list;
      coord_list.emplace_back(first_real_rect.get_ll_x(), first_mid_coord.get_y());
      coord_list.emplace_back(first_real_rect.get_ur_x(), first_mid_coord.get_y());
      coord_list.emplace_back(second_real_rect.get_ll_x(), second_mid_coord.get_y());
      coord_list.emplace_back(second_real_rect.get_ur_x(), second_mid_coord.get_y());
      search_real_rect = RTUTIL.getBoundingBox(coord_list);
    } else {
      RTLOG.error(Loc::current(), "The grid_pair is not horizontal or vertical!");
    }
  }
  EXTLayerRect search_rect;
  search_rect.set_real_rect(search_real_rect);
  search_rect.set_grid_rect(RTUTIL.getClosedGCellGridRect(search_rect.get_real_rect(), gcell_axis));
  search_rect.set_layer_idx(first_coord.get_layer_idx());
  return search_rect;
}

std::vector<LayerRect> GlobalRouter::getCrossingWireList(const EXTLayerRect& search_rect)
{
  RoutingLayer& routing_layer = RTDM.getDatabase().get_routing_layer_list()[search_rect.get_layer_idx()];
  int32_t half_wire_width = routing_layer.get_min_width() / 2;

  int32_t real_ll_x = search_rect.get_real_ll_x();
  int32_t real_ll_y = search_rect.get_real_ll_y();
  int32_t real_ur_x = search_rect.get_real_ur_x();
  int32_t real_ur_y = search_rect.get_real_ur_y();

  std::vector<LayerRect> wire_list;
  if (routing_layer.isPreferH()) {
    for (int32_t y : RTUTIL.getScaleList(real_ll_y, real_ur_y, routing_layer.getYTrackGridList())) {
      wire_list.emplace_back(real_ll_x, y - half_wire_width, real_ur_x, y + half_wire_width, search_rect.get_layer_idx());
    }
  } else {
    for (int32_t x : RTUTIL.getScaleList(real_ll_x, real_ur_x, routing_layer.getXTrackGridList())) {
      wire_list.emplace_back(x - half_wire_width, real_ll_y, x + half_wire_width, real_ur_y, search_rect.get_layer_idx());
    }
  }
  return wire_list;
}

bool GlobalRouter::isAccess(const LayerRect& wire, const std::vector<PlanarRect>& enlarged_obs_rect_list)
{
  for (const PlanarRect& enlarged_obs_rect : enlarged_obs_rect_list) {
    if (RTUTIL.isOpenOverlap(enlarged_obs_rect, wire)) {
      return false;
    }
  }
  return true;
}

void GlobalRouter::initGRNetList()
{
  _gr_net_list.clear();
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();
  RegionRoute& region_route = RTDM.getDatabase().get_region_route();
  for (Net& net : net_list) {
    if (net.get_pin_list().size() < 2) {
      continue;
    }
    if (!region_route.isActiveNet(net.get_net_idx())) {
      continue;
    }
    GRNet gr_net;
    gr_net.origin_net = &net;
    gr_net.bbox = net.get_bounding_box();
    bool is_valid = true;
    for (Pin& pin : net.get_pin_list()) {
      AccessPoint& access_point = pin.get_access_point();
      PlanarCoord grid_coord = access_point.get_grid_coord();
      int32_t layer_idx = access_point.get_layer_idx();
      if (grid_coord.get_x() < 0 || grid_coord.get_x() >= _grid_graph.getXSize() || grid_coord.get_y() < 0 || grid_coord.get_y() >= _grid_graph.getYSize()
          || layer_idx < _grid_graph.getBottomLayerIdx() || layer_idx > _grid_graph.getTopLayerIdx()) {
        RTLOG.warn(Loc::current(), "Skip net ", net.get_net_name(), " because pin ", pin.get_pin_name(), " has invalid access point!");
        is_valid = false;
        break;
      }
      gr_net.pin_list.push_back(GRPin{&pin, grid_coord, layer_idx});
    }
    if (!is_valid) {
      continue;
    }
    buildExemptEdgeSet(gr_net);
    _gr_net_list.push_back(std::move(gr_net));
  }
  std::sort(_gr_net_list.begin(), _gr_net_list.end(), CmpGRNet());
  int32_t local_net_num = 0;
  for (const GRNet& gr_net : _gr_net_list) {
    std::set<PlanarCoord, CmpPlanarCoordByXASC> coord_set;
    for (const GRPin& gr_pin : gr_net.pin_list) {
      coord_set.insert(gr_pin.grid_coord);
    }
    if (coord_set.size() == 1) {
      local_net_num++;
    }
  }
  RTLOG.info(Loc::current(), "Global router net num: ", _gr_net_list.size(), ", local net num: ", local_net_num);
}

void GlobalRouter::buildExemptEdgeSet(GRNet& net)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  for (GRPin& gr_pin : net.pin_list) {
    int32_t layer_idx = gr_pin.layer_idx;
    bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
    PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(gr_pin.grid_coord, gcell_axis);
    PlanarCoord gcell_mid_coord = gcell_rect.getMidPoint();
    PlanarCoord real_coord = gr_pin.pin->get_access_point().get_real_coord();
    bool toward_low = false;
    if (is_horizontal) {
      if (real_coord.get_x() < gcell_mid_coord.get_x()) {
        toward_low = true;
      } else if (real_coord.get_x() > gcell_mid_coord.get_x()) {
        toward_low = false;
      } else {
        toward_low = net.bbox.get_grid_rect().getMidPoint().get_x() < gr_pin.grid_coord.get_x();
      }
    } else {
      if (real_coord.get_y() < gcell_mid_coord.get_y()) {
        toward_low = true;
      } else if (real_coord.get_y() > gcell_mid_coord.get_y()) {
        toward_low = false;
      } else {
        toward_low = net.bbox.get_grid_rect().getMidPoint().get_y() < gr_pin.grid_coord.get_y();
      }
    }

    int32_t edge_x = -1;
    int32_t edge_y = -1;
    if (is_horizontal) {
      int32_t x = gr_pin.grid_coord.get_x();
      edge_y = gr_pin.grid_coord.get_y();
      if (toward_low) {
        if (x > 0) {
          edge_x = x - 1;
        } else if (x < _grid_graph.getXSize() - 1) {
          edge_x = x;
        }
      } else {
        if (x < _grid_graph.getXSize() - 1) {
          edge_x = x;
        } else if (x > 0) {
          edge_x = x - 1;
        }
      }
    } else {
      int32_t y = gr_pin.grid_coord.get_y();
      edge_x = gr_pin.grid_coord.get_x();
      if (toward_low) {
        if (y > 0) {
          edge_y = y - 1;
        } else if (y < _grid_graph.getYSize() - 1) {
          edge_y = y;
        }
      } else {
        if (y < _grid_graph.getYSize() - 1) {
          edge_y = y;
        } else if (y > 0) {
          edge_y = y - 1;
        }
      }
    }
    if (edge_x < 0 || edge_y < 0) {
      continue;
    }
    GRWireEdgeId edge_id{edge_x, edge_y, layer_idx};
    if (_grid_graph.hasWireEdge(edge_id)) {
      net.exempt_edge_set.insert(edge_id);
    }
  }
}

void GlobalRouter::routeGRNetList()
{
  if (_gr_net_list.empty()) {
    return;
  }
  size_t next_percent = 10;
  for (size_t i = 0; i < _gr_net_list.size(); i++) {
    routeGRNet(_gr_net_list[i]);
    size_t percent = ((i + 1) * 100) / _gr_net_list.size();
    if (percent >= next_percent || i + 1 == _gr_net_list.size()) {
      RTLOG.info(Loc::current(), "Routed ", i + 1, "/", _gr_net_list.size(), "(", percent, "%)");
      next_percent += 10;
    }
  }
}

bool GlobalRouter::routeGRNet(GRNet& net)
{
  GRTree old_tree = net.routing_tree;
  ripupRoute(net, old_tree);

  GRTree new_tree;
  if (!buildRoutingTree(net, new_tree)) {
    commitRoute(net, old_tree);
    RTLOG.warn(Loc::current(), "Failed to route net ", net.origin_net->get_net_name());
    return false;
  }
  commitRoute(net, new_tree);
  return true;
}

void GlobalRouter::mazeRouteGRNetList()
{
  constexpr int32_t kMaxIteration = 3;
  for (int32_t iteration = 0; iteration < kMaxIteration; iteration++) {
    std::vector<GRNet*> net_list = getOverflowNetList();
    if (net_list.empty()) {
      break;
    }
    double before_overflow = getTotalOverflow();
    double before_cost = getTotalRoutingCost();
    std::vector<std::pair<GRNet*, GRTree>> snapshot_list;
    snapshot_list.reserve(net_list.size());
    for (GRNet* net : net_list) {
      snapshot_list.emplace_back(net, net->routing_tree);
      routeGRNetByMaze(*net);
    }
    double after_overflow = getTotalOverflow();
    double after_cost = getTotalRoutingCost();
    // Overflow is the primary reroute objective; cost only breaks ties when overflow is unchanged.
    bool accepted = after_overflow < before_overflow - kEpsilon || (after_overflow <= before_overflow + kEpsilon && after_cost < before_cost - kEpsilon);
    RTLOG.info(Loc::current(), "Maze reroute iteration ", iteration, ": nets ", net_list.size(), ", overflow ", before_overflow, " -> ", after_overflow,
               ", cost ", before_cost, " -> ", after_cost, accepted ? " (accepted)" : " (rolled back)");
    if (!accepted) {
      for (auto& [net, old_tree] : snapshot_list) {
        ripupRoute(*net, net->routing_tree);
        commitRoute(*net, old_tree);
      }
      break;
    }
  }
}

bool GlobalRouter::routeGRNetByMaze(GRNet& net)
{
  GRTree old_tree = net.routing_tree;
  ripupRoute(net, old_tree);

  GRTree new_tree;
  if (!buildMazeRoutingTree(net, new_tree)) {
    commitRoute(net, old_tree);
    RTLOG.warn(Loc::current(), "Failed to maze-route net ", net.origin_net->get_net_name());
    return false;
  }
  commitRoute(net, new_tree);
  return true;
}

bool GlobalRouter::buildMazeRoutingTree(GRNet& net, GRTree& routing_tree)
{
  routing_tree = GRTree();
  std::vector<std::vector<PlanarCoord>> path_list;
  if (!buildMazePathList(net, path_list)) {
    return false;
  }
  if (!buildMazeTopoTree(net, path_list)) {
    return false;
  }
  return runDP(net, routing_tree);
}

bool GlobalRouter::buildMazePathList(const GRNet& net, std::vector<std::vector<PlanarCoord>>& path_list)
{
  path_list.clear();
  std::vector<PlanarCoord> terminal_list;
  for (const GRPin& gr_pin : net.pin_list) {
    bool is_found = false;
    for (const PlanarCoord& terminal : terminal_list) {
      if (terminal == gr_pin.grid_coord) {
        is_found = true;
        break;
      }
    }
    if (!is_found) {
      terminal_list.push_back(gr_pin.grid_coord);
    }
  }
  if (terminal_list.empty()) {
    return false;
  }
  if (terminal_list.size() == 1) {
    return true;
  }

  int32_t x_size = _grid_graph.getXSize();
  int32_t y_size = _grid_graph.getYSize();
  int32_t state_num = 2 * x_size * y_size;
  std::vector<double> g(state_num, kInf);
  std::vector<int32_t> parent(state_num, -1);
  std::vector<bool> closed(state_num, false);

  std::vector<bool> terminal_connected(terminal_list.size(), false);
  std::set<PlanarCoord, CmpPlanarCoordByXASC> tree_coord_set;
  std::vector<PlanarCoord> tree_coord_list;
  tree_coord_set.insert(terminal_list.front());
  tree_coord_list.push_back(terminal_list.front());
  int32_t remaining_terminal_num = static_cast<int32_t>(terminal_list.size());
  for (size_t i = 0; i < terminal_list.size(); i++) {
    if (tree_coord_set.find(terminal_list[i]) != tree_coord_set.end()) {
      terminal_connected[i] = true;
      remaining_terminal_num--;
    }
  }

  struct MazeQueueNode
  {
    double priority = 0.0;
    int32_t state = -1;

    bool operator<(const MazeQueueNode& other) const { return priority > other.priority; }
  };

  while (remaining_terminal_num > 0) {
    int32_t target_idx = -1;
    int32_t target_dist = INT_MAX;
    for (size_t i = 0; i < terminal_list.size(); i++) {
      if (terminal_connected[i]) {
        continue;
      }
      const PlanarCoord& terminal = terminal_list[i];
      int32_t min_dist = INT_MAX;
      for (const PlanarCoord& tree_coord : tree_coord_list) {
        int32_t dist = std::abs(terminal.get_x() - tree_coord.get_x()) + std::abs(terminal.get_y() - tree_coord.get_y());
        min_dist = std::min(min_dist, dist);
      }
      if (min_dist < target_dist) {
        target_dist = min_dist;
        target_idx = static_cast<int32_t>(i);
      }
    }
    if (target_idx < 0) {
      return false;
    }
    const PlanarCoord target = terminal_list[target_idx];

    std::fill(g.begin(), g.end(), kInf);
    std::fill(parent.begin(), parent.end(), -1);
    std::fill(closed.begin(), closed.end(), false);
    std::priority_queue<MazeQueueNode> queue;
    for (const PlanarCoord& tree_coord : tree_coord_list) {
      for (int32_t dir = 0; dir < 2; dir++) {
        int32_t state = (tree_coord.get_y() * x_size + tree_coord.get_x()) * 2 + dir;
        g[state] = 0.0;
        parent[state] = -1;
        double priority = std::abs(tree_coord.get_x() - target.get_x()) + std::abs(tree_coord.get_y() - target.get_y());
        queue.push(MazeQueueNode{priority, state});
      }
    }

    int32_t goal_state = -1;
    while (!queue.empty()) {
      MazeQueueNode current = queue.top();
      queue.pop();
      int32_t state = current.state;
      if (closed[state]) {
        continue;
      }
      closed[state] = true;
      int32_t base = state / 2;
      int32_t x = base % x_size;
      int32_t y = base / x_size;
      int32_t dir = state % 2;
      if (x == target.get_x() && y == target.get_y()) {
        goal_state = state;
        break;
      }
      double current_cost = g[state];
      if (dir == 0) {
        if (x + 1 < x_size) {
          double wire_cost = getMazeWireCost(net, x, y, true);
          if (wire_cost < kInf) {
            int32_t next_state = (y * x_size + x + 1) * 2;
            double next_cost = current_cost + wire_cost;
            if (next_cost < g[next_state]) {
              g[next_state] = next_cost;
              parent[next_state] = state;
              double priority = next_cost + std::abs(x + 1 - target.get_x()) + std::abs(y - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
        if (x > 0) {
          double wire_cost = getMazeWireCost(net, x - 1, y, true);
          if (wire_cost < kInf) {
            int32_t next_state = (y * x_size + x - 1) * 2;
            double next_cost = current_cost + wire_cost;
            if (next_cost < g[next_state]) {
              g[next_state] = next_cost;
              parent[next_state] = state;
              double priority = next_cost + std::abs(x - 1 - target.get_x()) + std::abs(y - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
      } else {
        if (y + 1 < y_size) {
          double wire_cost = getMazeWireCost(net, x, y, false);
          if (wire_cost < kInf) {
            int32_t next_state = ((y + 1) * x_size + x) * 2 + 1;
            double next_cost = current_cost + wire_cost;
            if (next_cost < g[next_state]) {
              g[next_state] = next_cost;
              parent[next_state] = state;
              double priority = next_cost + std::abs(x - target.get_x()) + std::abs(y + 1 - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
        if (y > 0) {
          double wire_cost = getMazeWireCost(net, x, y - 1, false);
          if (wire_cost < kInf) {
            int32_t next_state = ((y - 1) * x_size + x) * 2 + 1;
            double next_cost = current_cost + wire_cost;
            if (next_cost < g[next_state]) {
              g[next_state] = next_cost;
              parent[next_state] = state;
              double priority = next_cost + std::abs(x - target.get_x()) + std::abs(y - 1 - target.get_y());
              queue.push(MazeQueueNode{priority, next_state});
            }
          }
        }
      }
      int32_t next_state = state ^ 1;
      double next_cost = current_cost + kViaBaseCost;
      if (next_cost < g[next_state]) {
        g[next_state] = next_cost;
        parent[next_state] = state;
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
      state = parent[state];
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
      if (tree_coord_set.insert(coord).second) {
        tree_coord_list.push_back(coord);
      }
    }
    for (size_t i = 0; i < terminal_list.size(); i++) {
      if (!terminal_connected[i] && tree_coord_set.find(terminal_list[i]) != tree_coord_set.end()) {
        terminal_connected[i] = true;
        remaining_terminal_num--;
      }
    }
  }
  return true;
}

bool GlobalRouter::buildMazeTopoTree(GRNet& net, const std::vector<std::vector<PlanarCoord>>& path_list)
{
  net.topology = GRTopoTree();
  std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC> coord_node_map;
  std::vector<std::set<int32_t>> adjacency;
  for (const std::vector<PlanarCoord>& path : path_list) {
    int32_t prev_idx = -1;
    for (const PlanarCoord& coord : path) {
      int32_t node_idx = getOrCreateTopoNode(net.topology, coord_node_map, coord);
      if (adjacency.size() < net.topology.node_list.size()) {
        adjacency.resize(net.topology.node_list.size());
      }
      if (prev_idx >= 0 && prev_idx != node_idx) {
        adjacency[prev_idx].insert(node_idx);
        adjacency[node_idx].insert(prev_idx);
      }
      prev_idx = node_idx;
    }
  }
  if (net.topology.node_list.empty()) {
    if (net.pin_list.empty()) {
      return false;
    }
    getOrCreateTopoNode(net.topology, coord_node_map, net.pin_list.front().grid_coord);
    adjacency.resize(net.topology.node_list.size());
  }

  auto root_iter = coord_node_map.find(net.pin_list.front().grid_coord);
  if (root_iter == coord_node_map.end()) {
    return false;
  }
  int32_t root_idx = root_iter->second;
  net.topology.root_idx = root_idx;
  int32_t node_num = static_cast<int32_t>(net.topology.node_list.size());
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
      addTopoEdge(net.topology, node_idx, neighbor_idx);
      queue.push(neighbor_idx);
    }
  }
  if (visited_num != node_num) {
    return false;
  }

  for (GRPin& gr_pin : net.pin_list) {
    auto iter = coord_node_map.find(gr_pin.grid_coord);
    if (iter == coord_node_map.end()) {
      return false;
    }
    GRTopoNode& node = net.topology.node_list[iter->second];
    node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
    node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
  }
  return true;
}

double GlobalRouter::getMazeWireCost(const GRNet& net, int32_t x, int32_t y, bool is_horizontal) const
{
  double total_supply = 0.0;
  double total_demand = 0.0;
  double min_short_cost = kInf;
  bool has_edge = false;
  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    if (_grid_graph.isHorizontalLayer(layer_idx) != is_horizontal) {
      continue;
    }
    GRWireEdgeId edge_id{x, y, layer_idx};
    if (!_grid_graph.hasWireEdge(edge_id)) {
      continue;
    }
    if (isExemptWireEdge(net, edge_id)) {
      return kUnitWireCost;
    }
    const GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
    has_edge = true;
    total_supply += edge.supply;
    total_demand += edge.getDemand();
    min_short_cost = std::min(min_short_cost, _short_cost_list[layer_idx]);
  }
  if (!has_edge || total_supply <= kEpsilon) {
    return kInf;
  }
  return kUnitWireCost + min_short_cost * getLogistic(total_supply - total_demand);
}

std::vector<GRNet*> GlobalRouter::getOverflowNetList()
{
  std::vector<GRNet*> net_list;
  for (GRNet& net : _gr_net_list) {
    if (isNetOverflowing(net)) {
      net_list.push_back(&net);
    }
  }
  return net_list;
}

bool GlobalRouter::isNetOverflowing(const GRNet& net) const
{
  std::set<GRWireEdgeId> edge_set;
  for (const GRWireSegment& wire_segment : net.routing_tree.wire_list) {
    int32_t layer_idx = wire_segment.first.get_layer_idx();
    if (layer_idx != wire_segment.second.get_layer_idx()) {
      continue;
    }
    bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
    if (wire_segment.first.get_y() == wire_segment.second.get_y()) {
      if (!is_horizontal) {
        continue;
      }
      int32_t y = wire_segment.first.get_y();
      int32_t begin_x = std::min(wire_segment.first.get_x(), wire_segment.second.get_x());
      int32_t end_x = std::max(wire_segment.first.get_x(), wire_segment.second.get_x());
      for (int32_t x = begin_x; x < end_x; x++) {
        GRWireEdgeId edge_id{x, y, layer_idx};
        if (!_grid_graph.hasWireEdge(edge_id)) {
          continue;
        }
        if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > kEpsilon) {
          return true;
        }
      }
    } else if (wire_segment.first.get_x() == wire_segment.second.get_x()) {
      if (is_horizontal) {
        continue;
      }
      int32_t x = wire_segment.first.get_x();
      int32_t begin_y = std::min(wire_segment.first.get_y(), wire_segment.second.get_y());
      int32_t end_y = std::max(wire_segment.first.get_y(), wire_segment.second.get_y());
      for (int32_t y = begin_y; y < end_y; y++) {
        GRWireEdgeId edge_id{x, y, layer_idx};
        if (!_grid_graph.hasWireEdge(edge_id)) {
          continue;
        }
        if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > kEpsilon) {
          return true;
        }
      }
    }
  }
  for (const GRViaStack& via_stack : net.routing_tree.via_list) {
    if (via_stack.bottom_layer_idx < _grid_graph.getBottomLayerIdx() || via_stack.top_layer_idx > _grid_graph.getTopLayerIdx()
        || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      continue;
    }
    for (int32_t below_layer_idx = via_stack.bottom_layer_idx; below_layer_idx < via_stack.top_layer_idx; below_layer_idx++) {
      for (int32_t side_layer_idx : {below_layer_idx, below_layer_idx + 1}) {
        if (isAPViaSide(net, via_stack.coord, side_layer_idx)) {
          continue;
        }
        bool is_horizontal = _grid_graph.isHorizontalLayer(side_layer_idx);
        if (is_horizontal) {
          for (int32_t edge_x : {via_stack.coord.get_x() - 1, via_stack.coord.get_x()}) {
            GRWireEdgeId edge_id{edge_x, via_stack.coord.get_y(), side_layer_idx};
            if (!_grid_graph.hasWireEdge(edge_id)) {
              continue;
            }
            if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > kEpsilon) {
              return true;
            }
          }
        } else {
          for (int32_t edge_y : {via_stack.coord.get_y() - 1, via_stack.coord.get_y()}) {
            GRWireEdgeId edge_id{via_stack.coord.get_x(), edge_y, side_layer_idx};
            if (!_grid_graph.hasWireEdge(edge_id)) {
              continue;
            }
            if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > kEpsilon) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

double GlobalRouter::getTotalOverflow() const
{
  double total_overflow = 0.0;
  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        total_overflow += edge_map[x][y].getOverflow();
      }
    }
  }
  return total_overflow;
}

double GlobalRouter::getTotalRoutingCost() const
{
  double total_cost = 0.0;
  for (const GRNet& net : _gr_net_list) {
    double net_cost = getRoutingTreeCost(net, net.routing_tree);
    if (net_cost >= kInf) {
      return kInf;
    }
    total_cost += net_cost;
  }
  return total_cost;
}

double GlobalRouter::getRoutingTreeCost(const GRNet& net, const GRTree& tree) const
{
  double total_cost = 0.0;
  for (const GRWireSegment& wire_segment : tree.wire_list) {
    if (wire_segment.first.get_layer_idx() != wire_segment.second.get_layer_idx()) {
      continue;
    }
    double wire_cost = getWireSegmentCost(net, wire_segment.first.get_layer_idx(), wire_segment.first, wire_segment.second);
    if (wire_cost >= kInf) {
      return kInf;
    }
    total_cost += wire_cost;
  }
  for (const GRViaStack& via_stack : tree.via_list) {
    if (via_stack.bottom_layer_idx < _grid_graph.getBottomLayerIdx() || via_stack.top_layer_idx > _grid_graph.getTopLayerIdx()
        || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      continue;
    }
    for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx < via_stack.top_layer_idx; layer_idx++) {
      double via_cost = getViaCost(net, via_stack.coord, layer_idx);
      if (via_cost >= kInf) {
        return kInf;
      }
      total_cost += via_cost;
    }
  }
  return total_cost;
}

bool GlobalRouter::buildRoutingTree(GRNet& net, GRTree& routing_tree)
{
  routing_tree = GRTree();
  if (!buildTopoTree(net)) {
    return false;
  }
  return runDP(net, routing_tree);
}

bool GlobalRouter::buildTopoTree(GRNet& net)
{
  net.topology = GRTopoTree();
  std::vector<PlanarCoord> coord_list;
  coord_list.reserve(net.pin_list.size());
  for (GRPin& gr_pin : net.pin_list) {
    coord_list.push_back(gr_pin.grid_coord);
  }
  std::sort(coord_list.begin(), coord_list.end(), CmpPlanarCoordByXASC());
  coord_list.erase(std::unique(coord_list.begin(), coord_list.end()), coord_list.end());
  if (coord_list.empty()) {
    RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " has no unique pin coordinate!");
    return false;
  }

  if (coord_list.size() == 1) {
    GRTopoNode root_node;
    root_node.coord = coord_list.front();
    net.topology.node_list.push_back(root_node);
    net.topology.root_idx = 0;
    for (GRPin& gr_pin : net.pin_list) {
      GRTopoNode& node = net.topology.node_list[0];
      node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
      node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
    }
    return true;
  }

  std::vector<Flute::DTYPE> x_list(coord_list.size());
  std::vector<Flute::DTYPE> y_list(coord_list.size());
  for (size_t i = 0; i < coord_list.size(); i++) {
    x_list[i] = coord_list[i].get_x();
    y_list[i] = coord_list[i].get_y();
  }
  Flute::Tree flute_tree = Flute::flute(static_cast<int32_t>(coord_list.size()), x_list.data(), y_list.data(), FLUTE_ACCURACY);
  if (flute_tree.deg <= 0) {
    RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " FLUTE tree is empty!");
    Flute::free_tree(flute_tree);
    return false;
  }
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
  if (branch_order_list.size() != static_cast<size_t>(branch_num)) {
    RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " FLUTE tree is not connected!");
    Flute::free_tree(flute_tree);
    return false;
  }

  std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC> coord_node_map;
  for (int32_t branch_idx : branch_order_list) {
    PlanarCoord coord(flute_tree.branch[branch_idx].x, flute_tree.branch[branch_idx].y);
    int32_t node_idx = getOrCreateTopoNode(net.topology, coord_node_map, coord);
    if (branch_idx == 0) {
      net.topology.root_idx = node_idx;
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
    if (parent_coord.get_x() != child_coord.get_x() && parent_coord.get_y() != child_coord.get_y()) {
      std::vector<int32_t> candidate_list;
      candidate_list.push_back(addBendNode(net.topology, parent_node_idx, child_node_idx, PlanarCoord(child_coord.get_x(), parent_coord.get_y())));
      candidate_list.push_back(addBendNode(net.topology, parent_node_idx, child_node_idx, PlanarCoord(parent_coord.get_x(), child_coord.get_y())));

      constexpr double kFractionList[] = {1.0 / 8.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 2.0, 5.0 / 8.0, 3.0 / 4.0, 7.0 / 8.0};
      std::set<int32_t> used_x;
      for (double fraction : kFractionList) {
        int32_t x = parent_coord.get_x() + static_cast<int32_t>(std::lround((child_coord.get_x() - parent_coord.get_x()) * fraction));
        if (x < 0 || x >= _grid_graph.getXSize() || x == parent_coord.get_x() || x == child_coord.get_x()) {
          continue;
        }
        if (!used_x.insert(x).second) {
          continue;
        }
        std::vector<PlanarCoord> path;
        path.emplace_back(x, parent_coord.get_y());
        path.emplace_back(x, child_coord.get_y());
        candidate_list.push_back(addPatternPath(net.topology, parent_node_idx, child_node_idx, path));
      }

      std::set<int32_t> used_y;
      for (double fraction : kFractionList) {
        int32_t y = parent_coord.get_y() + static_cast<int32_t>(std::lround((child_coord.get_y() - parent_coord.get_y()) * fraction));
        if (y < 0 || y >= _grid_graph.getYSize() || y == parent_coord.get_y() || y == child_coord.get_y()) {
          continue;
        }
        if (!used_y.insert(y).second) {
          continue;
        }
        std::vector<PlanarCoord> path;
        path.emplace_back(parent_coord.get_x(), y);
        path.emplace_back(child_coord.get_x(), y);
        candidate_list.push_back(addPatternPath(net.topology, parent_node_idx, child_node_idx, path));
      }
      addTopoEdge(net.topology, parent_node_idx, child_node_idx, candidate_list);
    } else {
      addTopoEdge(net.topology, parent_node_idx, child_node_idx);
    }
  }

  for (GRPin& gr_pin : net.pin_list) {
    auto iter = coord_node_map.find(gr_pin.grid_coord);
    if (iter == coord_node_map.end()) {
      RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " pin coordinate is not in the FLUTE tree!");
      Flute::free_tree(flute_tree);
      return false;
    }
    GRTopoNode& node = net.topology.node_list[iter->second];
    node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
    node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
  }
  Flute::free_tree(flute_tree);

  if (net.topology.root_idx < 0) {
    return false;
  }
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(net.topology.node_list.size()); node_idx++) {
    const GRTopoNode& node = net.topology.node_list[node_idx];
    if (node.is_bend) {
      continue;
    }
    if (node_idx != net.topology.root_idx && node.parent == -1) {
      return false;
    }
  }
  return true;
}

bool GlobalRouter::runDP(const GRNet& net, GRTree& routing_tree)
{
  const GRTopoTree& topo_tree = net.topology;
  int32_t node_num = static_cast<int32_t>(topo_tree.node_list.size());
  int32_t layer_num = _grid_graph.getLayerNum();
  int32_t bottom_layer_idx = _grid_graph.getBottomLayerIdx();
  int32_t top_layer_idx = _grid_graph.getTopLayerIdx();
  if (node_num == 0 || topo_tree.root_idx < 0) {
    return false;
  }

  int32_t logical_node_num = 0;
  for (const GRTopoNode& node : topo_tree.node_list) {
    if (!node.is_bend) {
      logical_node_num++;
    }
  }

  std::vector<int32_t> postorder;
  getPostorder(topo_tree.root_idx, topo_tree, postorder);
  if (postorder.size() != static_cast<size_t>(logical_node_num)) {
    RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " topo tree is disconnected!");
    return false;
  }

  GRDpState state(topo_tree, node_num, layer_num, bottom_layer_idx, top_layer_idx);
  for (int32_t node_idx : postorder) {
    computeNodeDP(net, state, node_idx);
  }

  int32_t root_layer_idx = -1;
  double root_cost = kInf;
  for (int32_t layer_idx = bottom_layer_idx; layer_idx <= top_layer_idx; layer_idx++) {
    if (state.d[topo_tree.root_idx][layer_idx] < root_cost) {
      root_cost = state.d[topo_tree.root_idx][layer_idx];
      root_layer_idx = layer_idx;
    }
  }
  if (root_layer_idx < 0) {
    RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " has no feasible layer assignment!");
    return false;
  }
  reconstructTree(net, state.best_b, state.best_t, state.best_child_layer, state.best_child_bend, topo_tree.root_idx, root_layer_idx, routing_tree);
  return true;
}

void GlobalRouter::computeNodeDP(const GRNet& net, GRDpState& state, int32_t node_idx)
{
  if (state.computed[node_idx]) {
    return;
  }
  const GRTopoNode& node = state.topo_tree.node_list[node_idx];
  int32_t child_num = static_cast<int32_t>(node.child_list.size());

  std::vector<double> via_cost(state.layer_num, 0.0);
  for (int32_t layer_idx = state.bottom_layer_idx; layer_idx < state.top_layer_idx; layer_idx++) {
    via_cost[layer_idx] = getViaCost(net, node.coord, layer_idx);
  }
  std::vector<double> phi(state.layer_num + 1, 0.0);
  std::vector<int32_t> blocked_via_prefix(state.layer_num + 1, 0);
  for (int32_t layer_idx = 0; layer_idx < state.layer_num; layer_idx++) {
    if (via_cost[layer_idx] >= kInf) {
      phi[layer_idx + 1] = phi[layer_idx];
      blocked_via_prefix[layer_idx + 1] = blocked_via_prefix[layer_idx] + 1;
    } else {
      phi[layer_idx + 1] = phi[layer_idx] + via_cost[layer_idx];
      blocked_via_prefix[layer_idx + 1] = blocked_via_prefix[layer_idx];
    }
  }

  std::vector<std::vector<double>> child_g(child_num, std::vector<double>(state.layer_num, kInf));
  std::vector<std::vector<int32_t>> child_bend(child_num, std::vector<int32_t>(state.layer_num, -1));
  std::vector<std::vector<std::vector<double>>> child_range_min(child_num,
                                                                std::vector<std::vector<double>>(state.layer_num, std::vector<double>(state.layer_num, kInf)));
  for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    if (!state.computed[child_idx]) {
      computeNodeDP(net, state, child_idx);
    }
    const GRTopoNode& child_node = state.topo_tree.node_list[child_idx];
    const std::vector<int32_t>& bend_idx_list = node.child_bend_list[child_pos];
    if (bend_idx_list.empty()) {
      for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
        double wire_cost = getWireSegmentCost(net, layer_idx, node.coord, child_node.coord);
        child_g[child_pos][layer_idx] = addCost(state.d[child_idx][layer_idx], wire_cost);
      }
    } else {
      for (int32_t bend_idx : bend_idx_list) {
        computeNodeDP(net, state, bend_idx);
      }
      for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
        double best_cost = kInf;
        int32_t best_bend_idx = -1;
        for (int32_t bend_idx : bend_idx_list) {
          const PlanarCoord& bend_coord = state.topo_tree.node_list[bend_idx].coord;
          double wire_cost = getWireSegmentCost(net, layer_idx, node.coord, bend_coord);
          double cost = addCost(state.d[bend_idx][layer_idx], wire_cost);
          if (cost < best_cost) {
            best_cost = cost;
            best_bend_idx = bend_idx;
          }
        }
        child_g[child_pos][layer_idx] = best_cost;
        child_bend[child_pos][layer_idx] = best_bend_idx;
      }
    }
    for (int32_t begin_layer = 0; begin_layer < state.layer_num; begin_layer++) {
      double min_cost = kInf;
      for (int32_t end_layer = begin_layer; end_layer < state.layer_num; end_layer++) {
        min_cost = std::min(min_cost, child_g[child_pos][end_layer]);
        child_range_min[child_pos][begin_layer][end_layer] = min_cost;
      }
    }
  }

  std::vector<std::vector<double>> sum_min(state.layer_num, std::vector<double>(state.layer_num, kInf));
  for (int32_t begin_layer = state.bottom_layer_idx; begin_layer <= state.top_layer_idx; begin_layer++) {
    for (int32_t end_layer = begin_layer; end_layer <= state.top_layer_idx; end_layer++) {
      double sum = 0.0;
      bool is_valid = true;
      for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
        double child_min = child_range_min[child_pos][begin_layer][end_layer];
        if (child_min >= kInf) {
          is_valid = false;
          break;
        }
        sum += child_min;
      }
      if (is_valid) {
        sum_min[begin_layer][end_layer] = sum;
      }
    }
  }

  for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
    int32_t begin_limit = node.fixed_low == INT_MAX ? layer_idx : std::min(node.fixed_low, layer_idx);
    int32_t end_start = node.fixed_high == INT_MIN ? layer_idx : std::max(node.fixed_high, layer_idx);
    begin_limit = std::min(begin_limit, state.top_layer_idx);
    end_start = std::max(end_start, state.bottom_layer_idx);

    double best_cost = kInf;
    int32_t best_begin = -1;
    int32_t best_end = -1;
    for (int32_t begin_layer = state.bottom_layer_idx; begin_layer <= begin_limit; begin_layer++) {
      for (int32_t end_layer = end_start; end_layer <= state.top_layer_idx; end_layer++) {
        if (blocked_via_prefix[end_layer] != blocked_via_prefix[begin_layer]) {
          continue;
        }
        double stack_cost = phi[end_layer] - phi[begin_layer];
        double total_cost = addCost(stack_cost, sum_min[begin_layer][end_layer]);
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
    state.best_child_layer[node_idx][layer_idx].resize(child_num, -1);
    state.best_child_bend[node_idx][layer_idx].resize(child_num, -1);
    for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
      double min_child_cost = kInf;
      int32_t best_child_layer_idx = -1;
      for (int32_t child_layer_idx = best_begin; child_layer_idx <= best_end; child_layer_idx++) {
        if (child_g[child_pos][child_layer_idx] < min_child_cost) {
          min_child_cost = child_g[child_pos][child_layer_idx];
          best_child_layer_idx = child_layer_idx;
        }
      }
      state.best_child_layer[node_idx][layer_idx][child_pos] = best_child_layer_idx;
      if (best_child_layer_idx >= 0) {
        state.best_child_bend[node_idx][layer_idx][child_pos] = child_bend[child_pos][best_child_layer_idx];
      }
    }
  }
  state.computed[node_idx] = true;
}

void GlobalRouter::getPostorder(int32_t node_idx, const GRTopoTree& topo_tree, std::vector<int32_t>& postorder)
{
  for (int32_t child_idx : topo_tree.node_list[node_idx].child_list) {
    getPostorder(child_idx, topo_tree, postorder);
  }
  postorder.push_back(node_idx);
}

int32_t GlobalRouter::getOrCreateTopoNode(GRTopoTree& topo_tree, std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, const PlanarCoord& coord)
{
  auto iter = coord_node_map.find(coord);
  if (iter != coord_node_map.end()) {
    return iter->second;
  }
  GRTopoNode node;
  node.coord = coord;
  int32_t node_idx = static_cast<int32_t>(topo_tree.node_list.size());
  topo_tree.node_list.push_back(node);
  coord_node_map[coord] = node_idx;
  return node_idx;
}

int32_t GlobalRouter::addBendNode(GRTopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, const PlanarCoord& coord)
{
  GRTopoNode bend_node;
  bend_node.coord = coord;
  bend_node.parent = parent_idx;
  bend_node.is_bend = true;
  bend_node.child_list.push_back(child_idx);
  bend_node.child_bend_list.emplace_back();
  int32_t bend_idx = static_cast<int32_t>(topo_tree.node_list.size());
  topo_tree.node_list.push_back(std::move(bend_node));
  return bend_idx;
}

int32_t GlobalRouter::addPatternPath(GRTopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, const std::vector<PlanarCoord>& path)
{
  int32_t next_idx = child_idx;
  for (int32_t i = static_cast<int32_t>(path.size()) - 1; i >= 0; i--) {
    next_idx = addBendNode(topo_tree, parent_idx, next_idx, path[i]);
  }
  return next_idx;
}

void GlobalRouter::addTopoEdge(GRTopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, std::vector<int32_t> bend_idx_list)
{
  if (parent_idx == child_idx) {
    return;
  }
  GRTopoNode& child_node = topo_tree.node_list[child_idx];
  if (child_node.parent != -1) {
    return;
  }
  child_node.parent = parent_idx;
  GRTopoNode& parent_node = topo_tree.node_list[parent_idx];
  parent_node.child_list.push_back(child_idx);
  parent_node.child_bend_list.push_back(std::move(bend_idx_list));
}

void GlobalRouter::reconstructTree(const GRNet& net, const std::vector<std::vector<int32_t>>& best_b, const std::vector<std::vector<int32_t>>& best_t,
                                   const std::vector<std::vector<std::vector<int32_t>>>& best_child_layer,
                                   const std::vector<std::vector<std::vector<int32_t>>>& best_child_bend, int32_t node_idx, int32_t layer_idx,
                                   GRTree& routing_tree)
{
  const GRTopoNode& node = net.topology.node_list[node_idx];
  int32_t begin_layer = best_b[node_idx][layer_idx];
  int32_t end_layer = best_t[node_idx][layer_idx];
  if (begin_layer < 0 || end_layer < 0) {
    return;
  }
  if (begin_layer < end_layer) {
    routing_tree.via_list.push_back(GRViaStack{node.coord, begin_layer, end_layer});
  }
  for (size_t child_pos = 0; child_pos < node.child_list.size(); child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    int32_t child_layer_idx = best_child_layer[node_idx][layer_idx][child_pos];
    if (child_layer_idx < 0) {
      continue;
    }
    int32_t bend_idx = best_child_bend[node_idx][layer_idx][child_pos];
    int32_t next_idx = bend_idx < 0 ? child_idx : bend_idx;
    const GRTopoNode& next_node = net.topology.node_list[next_idx];
    GRWireSegment wire_segment;
    wire_segment.first = LayerCoord(node.coord, child_layer_idx);
    wire_segment.second = LayerCoord(next_node.coord, child_layer_idx);
    if (wire_segment.first != wire_segment.second) {
      routing_tree.wire_list.push_back(wire_segment);
    }
    reconstructTree(net, best_b, best_t, best_child_layer, best_child_bend, next_idx, child_layer_idx, routing_tree);
  }
}

double GlobalRouter::getWireSegmentCost(const GRNet& net, int32_t layer_idx, const PlanarCoord& first_coord, const PlanarCoord& second_coord) const
{
  if (first_coord == second_coord) {
    return 0.0;
  }
  if (layer_idx < _grid_graph.getBottomLayerIdx() || layer_idx > _grid_graph.getTopLayerIdx()) {
    return kInf;
  }
  bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
  double total_cost = 0.0;
  if (first_coord.get_y() == second_coord.get_y()) {
    if (!is_horizontal) {
      return kInf;
    }
    int32_t y = first_coord.get_y();
    int32_t begin_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t end_x = std::max(first_coord.get_x(), second_coord.get_x());
    for (int32_t x = begin_x; x < end_x; x++) {
      double edge_cost = getWireEdgeCost(net, GRWireEdgeId{x, y, layer_idx});
      if (edge_cost >= kInf) {
        return kInf;
      }
      total_cost += edge_cost;
    }
    return total_cost;
  }
  if (first_coord.get_x() == second_coord.get_x()) {
    if (is_horizontal) {
      return kInf;
    }
    int32_t x = first_coord.get_x();
    int32_t begin_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t end_y = std::max(first_coord.get_y(), second_coord.get_y());
    for (int32_t y = begin_y; y < end_y; y++) {
      double edge_cost = getWireEdgeCost(net, GRWireEdgeId{x, y, layer_idx});
      if (edge_cost >= kInf) {
        return kInf;
      }
      total_cost += edge_cost;
    }
    return total_cost;
  }
  RTLOG.warn(Loc::current(), "The wire segment is not horizontal or vertical!");
  return kInf;
}

double GlobalRouter::getWireEdgeCost(const GRNet& net, const GRWireEdgeId& edge_id) const
{
  if (edge_id.layer_idx < _grid_graph.getBottomLayerIdx() || edge_id.layer_idx > _grid_graph.getTopLayerIdx()) {
    return kInf;
  }
  if (!_grid_graph.hasWireEdge(edge_id)) {
    return kInf;
  }
  if (isExemptWireEdge(net, edge_id)) {
    return kUnitWireCost;
  }
  const GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
  if (edge.supply <= kEpsilon) {
    return kInf;
  }
  return kUnitWireCost + _short_cost_list[edge_id.layer_idx] * getLogistic(edge.supply - edge.getDemand());
}

double GlobalRouter::getViaCost(const GRNet& net, const PlanarCoord& coord, int32_t below_layer_idx) const
{
  if (below_layer_idx < _grid_graph.getBottomLayerIdx() || below_layer_idx >= _grid_graph.getTopLayerIdx()) {
    return kInf;
  }
  double total_cost = kViaBaseCost;
  for (int32_t side_layer_idx : {below_layer_idx, below_layer_idx + 1}) {
    if (isAPViaSide(net, coord, side_layer_idx)) {
      continue;
    }
    bool is_horizontal = _grid_graph.isHorizontalLayer(side_layer_idx);
    if (is_horizontal) {
      if (coord.get_x() > 0) {
        double edge_cost = getWireEdgeCost(net, GRWireEdgeId{coord.get_x() - 1, coord.get_y(), side_layer_idx});
        if (edge_cost >= kInf) {
          return kInf;
        }
        total_cost += kViaDemand * edge_cost;
      }
      if (coord.get_x() < _grid_graph.getXSize() - 1) {
        double edge_cost = getWireEdgeCost(net, GRWireEdgeId{coord.get_x(), coord.get_y(), side_layer_idx});
        if (edge_cost >= kInf) {
          return kInf;
        }
        total_cost += kViaDemand * edge_cost;
      }
    } else {
      if (coord.get_y() > 0) {
        double edge_cost = getWireEdgeCost(net, GRWireEdgeId{coord.get_x(), coord.get_y() - 1, side_layer_idx});
        if (edge_cost >= kInf) {
          return kInf;
        }
        total_cost += kViaDemand * edge_cost;
      }
      if (coord.get_y() < _grid_graph.getYSize() - 1) {
        double edge_cost = getWireEdgeCost(net, GRWireEdgeId{coord.get_x(), coord.get_y(), side_layer_idx});
        if (edge_cost >= kInf) {
          return kInf;
        }
        total_cost += kViaDemand * edge_cost;
      }
    }
  }
  return total_cost;
}

double GlobalRouter::getLogistic(double value) const
{
  double exponent = value * kSlope;
  if (exponent > 50.0) {
    return 0.0;
  }
  if (exponent < -50.0) {
    return 1.0;
  }
  return 1.0 / (1.0 + std::exp(exponent));
}

double GlobalRouter::addCost(double first, double second) const
{
  if (first >= kInf || second >= kInf) {
    return kInf;
  }
  double sum = first + second;
  return sum >= kInf ? kInf : sum;
}

bool GlobalRouter::isExemptWireEdge(const GRNet& net, const GRWireEdgeId& edge_id) const
{
  return net.exempt_edge_set.contains(edge_id);
}

bool GlobalRouter::isAPViaSide(const GRNet& net, const PlanarCoord& coord, int32_t layer_idx) const
{
  for (const GRPin& gr_pin : net.pin_list) {
    if (gr_pin.grid_coord == coord && gr_pin.layer_idx == layer_idx) {
      return true;
    }
  }
  return false;
}

void GlobalRouter::ripupRoute(GRNet& net, const GRTree& routing_tree)
{
  std::set<GRWireEdgeId> used_edge_set;
  addWireDemand(routing_tree, used_edge_set, -1.0);
  for (const GRViaStack& via_stack : routing_tree.via_list) {
    addViaDemand(net, via_stack, used_edge_set, -kViaDemand);
  }
}

void GlobalRouter::commitRoute(GRNet& net, const GRTree& routing_tree)
{
  std::set<GRWireEdgeId> used_edge_set;
  addWireDemand(routing_tree, used_edge_set, 1.0);
  for (const GRViaStack& via_stack : routing_tree.via_list) {
    addViaDemand(net, via_stack, used_edge_set, kViaDemand);
  }
  net.routing_tree = routing_tree;
}

void GlobalRouter::addWireDemand(const GRTree& routing_tree, std::set<GRWireEdgeId>& used_edge_set, double delta)
{
  for (const GRWireSegment& wire_segment : routing_tree.wire_list) {
    int32_t layer_idx = wire_segment.first.get_layer_idx();
    if (layer_idx != wire_segment.second.get_layer_idx()) {
      continue;
    }
    bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
    if (wire_segment.first.get_y() == wire_segment.second.get_y()) {
      if (!is_horizontal) {
        continue;
      }
      int32_t y = wire_segment.first.get_y();
      int32_t begin_x = std::min(wire_segment.first.get_x(), wire_segment.second.get_x());
      int32_t end_x = std::max(wire_segment.first.get_x(), wire_segment.second.get_x());
      for (int32_t x = begin_x; x < end_x; x++) {
        GRWireEdgeId edge_id{x, y, layer_idx};
        if (!used_edge_set.insert(edge_id).second) {
          continue;
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
        edge.routing_demand += delta;
        if (edge.routing_demand < -kEpsilon) {
          RTLOG.error(Loc::current(), "The wire edge routing demand is negative!");
        }
        if (edge.routing_demand < 0.0) {
          edge.routing_demand = 0.0;
        }
      }
    } else if (wire_segment.first.get_x() == wire_segment.second.get_x()) {
      if (is_horizontal) {
        continue;
      }
      int32_t x = wire_segment.first.get_x();
      int32_t begin_y = std::min(wire_segment.first.get_y(), wire_segment.second.get_y());
      int32_t end_y = std::max(wire_segment.first.get_y(), wire_segment.second.get_y());
      for (int32_t y = begin_y; y < end_y; y++) {
        GRWireEdgeId edge_id{x, y, layer_idx};
        if (!used_edge_set.insert(edge_id).second) {
          continue;
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
        edge.routing_demand += delta;
        if (edge.routing_demand < -kEpsilon) {
          RTLOG.error(Loc::current(), "The wire edge routing demand is negative!");
        }
        if (edge.routing_demand < 0.0) {
          edge.routing_demand = 0.0;
        }
      }
    }
  }
}

void GlobalRouter::addViaDemand(const GRNet& net, const GRViaStack& via_stack, std::set<GRWireEdgeId>& used_edge_set, double delta)
{
  if (via_stack.bottom_layer_idx < _grid_graph.getBottomLayerIdx() || via_stack.top_layer_idx > _grid_graph.getTopLayerIdx()
      || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
    return;
  }
  for (int32_t below_layer_idx = via_stack.bottom_layer_idx; below_layer_idx < via_stack.top_layer_idx; below_layer_idx++) {
    for (int32_t side_layer_idx : {below_layer_idx, below_layer_idx + 1}) {
      if (isAPViaSide(net, via_stack.coord, side_layer_idx)) {
        continue;
      }
      bool is_horizontal = _grid_graph.isHorizontalLayer(side_layer_idx);
      if (is_horizontal) {
        for (int32_t edge_x : {via_stack.coord.get_x() - 1, via_stack.coord.get_x()}) {
          int32_t edge_y = via_stack.coord.get_y();
          GRWireEdgeId edge_id{edge_x, edge_y, side_layer_idx};
          if (!_grid_graph.hasWireEdge(edge_id)) {
            continue;
          }
          if (!used_edge_set.insert(edge_id).second) {
            continue;
          }
          GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
          edge.routing_demand += delta;
          if (edge.routing_demand < -kEpsilon) {
            RTLOG.error(Loc::current(), "The via projection routing demand is negative!");
          }
          if (edge.routing_demand < 0.0) {
            edge.routing_demand = 0.0;
          }
        }
      } else {
        for (int32_t edge_y : {via_stack.coord.get_y() - 1, via_stack.coord.get_y()}) {
          int32_t edge_x = via_stack.coord.get_x();
          GRWireEdgeId edge_id{edge_x, edge_y, side_layer_idx};
          if (!_grid_graph.hasWireEdge(edge_id)) {
            continue;
          }
          if (!used_edge_set.insert(edge_id).second) {
            continue;
          }
          GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
          edge.routing_demand += delta;
          if (edge.routing_demand < -kEpsilon) {
            RTLOG.error(Loc::current(), "The via projection routing demand is negative!");
          }
          if (edge.routing_demand < 0.0) {
            edge.routing_demand = 0.0;
          }
        }
      }
    }
  }
}

void GlobalRouter::outputNetResult(GRNet& net)
{
  int32_t net_idx = net.origin_net->get_net_idx();
  std::vector<Segment<LayerCoord>>& result_list = RTDM.getDatabase().get_net_global_result_map()[net_idx];
  result_list.reserve(result_list.size() + net.routing_tree.wire_list.size() + net.routing_tree.via_list.size());
  for (const GRWireSegment& wire_segment : net.routing_tree.wire_list) {
    if (wire_segment.first == wire_segment.second) {
      continue;
    }
    result_list.emplace_back(wire_segment.first, wire_segment.second);
  }
  for (const GRViaStack& via_stack : net.routing_tree.via_list) {
    if (via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      continue;
    }
    result_list.emplace_back(LayerCoord(via_stack.coord, via_stack.bottom_layer_idx), LayerCoord(via_stack.coord, via_stack.top_layer_idx));
  }
}

void GlobalRouter::printSummary()
{
  double total_supply = 0.0;
  double total_demand = 0.0;
  double total_overflow = 0.0;
  double max_overflow = 0.0;
  int32_t overflow_edge_num = 0;
  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        const GRWireEdge& edge = edge_map[x][y];
        total_supply += edge.supply;
        total_demand += edge.getDemand();
        double overflow = edge.getOverflow();
        total_overflow += overflow;
        max_overflow = std::max(max_overflow, overflow);
        if (overflow > 0.0) {
          overflow_edge_num++;
        }
      }
    }
  }
  RTLOG.info(Loc::current(), "Total supply: ", total_supply);
  RTLOG.info(Loc::current(), "Total demand: ", total_demand);
  RTLOG.info(Loc::current(), "Total overflow: ", total_overflow);
  RTLOG.info(Loc::current(), "Max overflow: ", max_overflow);
  RTLOG.info(Loc::current(), "Overflow edge num: ", overflow_edge_num);
}

bool GlobalRouter::isLayerInRange(int32_t layer_idx) const
{
  return layer_idx >= _grid_graph.getBottomLayerIdx() && layer_idx <= _grid_graph.getTopLayerIdx();
}

void GlobalRouter::debugPlotGRModel(const std::string& flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  int32_t point_size = 5;

  GPGDS gp_gds;

  // base_region
  {
    GPStruct base_region_struct("base_region");
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(die.get_real_rect());
    base_region_struct.push(gp_boundary);
    gp_gds.addStruct(base_region_struct);
  }

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    std::vector<int32_t> gcell_x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), gcell_axis.get_x_grid_list());
    std::vector<int32_t> gcell_y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), gcell_axis.get_y_grid_list());
    for (int32_t x : gcell_x_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : gcell_y_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }

  // track_axis
  {
    GPStruct track_axis_struct("track_axis_struct");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      if (!isLayerInRange(routing_layer.get_layer_idx())) {
        continue;
      }
      std::vector<int32_t> x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), routing_layer.getXTrackGridList());
      std::vector<int32_t> y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), routing_layer.getYTrackGridList());
      for (int32_t x : x_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
      for (int32_t y : y_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
    }
    gp_gds.addStruct(track_axis_struct);
  }

  // fixed_rect
  auto& type_layer_fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, fixed_rect_rtree] : type_layer_fixed_rect_rtree_map[is_routing]) {
      if (!isLayerInRange(layer_idx)) {
        continue;
      }
      std::map<int32_t, GPStruct> net_fixed_rect_struct_map;
      for (const auto& [rect, net_fixed_rect] : fixed_rect_rtree) {
        auto [net_idx, fixed_rect] = net_fixed_rect;
        auto struct_iter = net_fixed_rect_struct_map.try_emplace(net_idx, RTUTIL.getString("fixed_rect(net_", net_idx, ")")).first;
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(fixed_rect->get_real_rect());
        gp_boundary.set_layer_idx(is_routing ? RTGP.getGDSIdxByRouting(layer_idx) : RTGP.getGDSIdxByCut(layer_idx));
        struct_iter->second.push(gp_boundary);
      }
      for (auto& [net_idx, fixed_rect_struct] : net_fixed_rect_struct_map) {
        gp_gds.addStruct(fixed_rect_struct);
      }
    }
  }

  // access_point
  for (auto& [net_idx, access_point_set] : RTDM.getNetAccessPointMap(die)) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", net_idx, ")"));
    for (AccessPoint* access_point : access_point_set) {
      if (!isLayerInRange(access_point->get_layer_idx())) {
        continue;
      }
      int32_t x = access_point->get_real_x();
      int32_t y = access_point->get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point->get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - point_size, y - point_size, x + point_size, y + point_size);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // global_result
  for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
    GPStruct global_result_struct(RTUTIL.getString("global_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      for (NetShape& net_shape : RTDM.getNetGlobalShapeList(net_idx, *segment)) {
        if (!isLayerInRange(net_shape.get_layer_idx())) {
          continue;
        }
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kGlobalPath));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        global_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(global_result_struct);
  }

  // overflow
  {
    GPStruct overflow_struct("overflow");
    for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
      const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
      bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
          const GRWireEdge& edge = edge_map[x][y];
          if (edge.getOverflow() <= kEpsilon) {
            continue;
          }
          PlanarCoord first_coord = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis).getMidPoint();
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarCoord second_coord = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis).getMidPoint();

          GPPath gp_path;
          gp_path.set_data_type(static_cast<int32_t>(GPDataType::kOverflow));
          gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_path.set_width(point_size);
          gp_path.set_segment(first_coord, second_coord);
          overflow_struct.push(gp_path);

          GPText gp_text;
          gp_text.set_coord(first_coord.get_x(), first_coord.get_y() + point_size);
          gp_text.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text.set_message(RTUTIL.getString("overflow: ", edge.getOverflow(), ", supply: ", edge.supply, ", demand: ", edge.getDemand()));
          gp_text.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_text.set_presentation(GPTextPresentation::kLeftMiddle);
          overflow_struct.push(gp_text);
        }
      }
    }
    gp_gds.addStruct(overflow_struct);
  }

  std::string gr_temp_directory_path = RTUTIL.getString(RTDM.getConfig().temp_directory_path, "global_router/");
  RTUTIL.createDir(gr_temp_directory_path);
  std::string gds_file_path = RTUTIL.getString(gr_temp_directory_path, flag, "_gr_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
  RTLOG.info(Loc::current(), "GR debug GDS: ", gds_file_path);
}

}  // namespace irt
