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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "GDSPlotter.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "RoutingLayer.hpp"
#include "Segment.hpp"
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
  _gr_params.reset_cost_weight();
  initGRGridGraph();

  analyzeSupply();
  initGRNetList();

  patternRouteGRNetList();
  detourRouteGRNetList();
  mazeRouteGRNetList();
  // debugPlotGRModel("after_route");

  outputGlobalResult();
  RTDM.rebuildGlobalResultRTree();
  printSummary();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

void GlobalRouter::initGRGridGraph()
{
  Database& database = RTDM.getDatabase();
  int32_t x_size = database.get_gcell_map().get_x_size();
  int32_t y_size = database.get_gcell_map().get_y_size();
  int32_t layer_num = static_cast<int32_t>(database.get_routing_layer_list().size());
  int32_t bottom_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_layer_idx = RTDM.getConfig().top_routing_layer_idx;

  std::vector<bool> is_horizontal_layer(layer_num, false);
  for (int32_t layer_idx = 0; layer_idx < layer_num; layer_idx++) {
    is_horizontal_layer[layer_idx] = database.get_routing_layer_list()[layer_idx].isPreferH();
  }

  _grid_graph._x_size = x_size;
  _grid_graph._y_size = y_size;
  _grid_graph._layer_num = layer_num;
  _grid_graph._bottom_layer_idx = bottom_layer_idx;
  _grid_graph._top_layer_idx = top_layer_idx;
  _grid_graph._is_horizontal_layer = is_horizontal_layer;

  _grid_graph._edge_map.resize(layer_num);
  for (int32_t layer_idx = 0; layer_idx < layer_num; layer_idx++) {
    if (is_horizontal_layer[layer_idx]) {
      _grid_graph._edge_map[layer_idx].init(std::max(0, x_size - 1), y_size);
    } else {
      _grid_graph._edge_map[layer_idx].init(x_size, std::max(0, y_size - 1));
    }
  }
}

void GlobalRouter::analyzeSupply()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
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
        int32_t blocked_wire_num = 0;
        for (const LayerRect& wire : wire_list) {
          for (const PlanarRect& enlarged_obs_rect : enlarged_obs_rect_list) {
            if (RTUTIL.isOpenOverlap(enlarged_obs_rect, wire)) {
              blocked_wire_num++;
              break;
            }
          }
        }

        int32_t supply_num = static_cast<int32_t>(wire_list.size());
        int32_t accessible_num = supply_num - blocked_wire_num;
        // Reserve one accessible track; it and all inaccessible tracks become fixed demand.
        int32_t usable_num = std::max(0, accessible_num - 1);
        GRWireEdge& wire_edge = _grid_graph.getWireEdge(GRWireEdgeId{x, y, layer_idx});
        wire_edge.supply = static_cast<double>(supply_num);
        wire_edge.fixed_demand = static_cast<double>(supply_num - usable_num);
      }
    }

  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

EXTLayerRect GlobalRouter::getSearchRect(const LayerCoord& first_coord, const LayerCoord& second_coord)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
  PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
  PlanarCoord first_mid_coord = first_real_rect.getMidPoint();
  PlanarCoord second_mid_coord = second_real_rect.getMidPoint();

  PlanarRect search_real_rect;
  if (RTUTIL.isHorizontal(first_coord, second_coord)) {
    search_real_rect.set_ll(std::min(first_mid_coord.get_x(), second_mid_coord.get_x()), std::min(first_real_rect.get_ll_y(), second_real_rect.get_ll_y()));
    search_real_rect.set_ur(std::max(first_mid_coord.get_x(), second_mid_coord.get_x()), std::max(first_real_rect.get_ur_y(), second_real_rect.get_ur_y()));
  } else if (RTUTIL.isVertical(first_coord, second_coord)) {
    search_real_rect.set_ll(std::min(first_real_rect.get_ll_x(), second_real_rect.get_ll_x()), std::min(first_mid_coord.get_y(), second_mid_coord.get_y()));
    search_real_rect.set_ur(std::max(first_real_rect.get_ur_x(), second_real_rect.get_ur_x()), std::max(first_mid_coord.get_y(), second_mid_coord.get_y()));
  } else {
    RTLOG.error(Loc::current(), "The grid_pair is not horizontal or vertical!");
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
  const PlanarRect& real_rect = search_rect.get_real_rect();
  int32_t layer_idx = search_rect.get_layer_idx();
  int32_t half_wire_width = routing_layer.get_min_width() / 2;

  std::vector<LayerRect> wire_list;
  if (routing_layer.isPreferH()) {
    for (int32_t y : RTUTIL.getScaleList(real_rect.get_ll_y(), real_rect.get_ur_y(), routing_layer.getYTrackGridList())) {
      wire_list.emplace_back(real_rect.get_ll_x(), y - half_wire_width, real_rect.get_ur_x(), y + half_wire_width, layer_idx);
    }
  } else {
    for (int32_t x : RTUTIL.getScaleList(real_rect.get_ll_x(), real_rect.get_ur_x(), routing_layer.getXTrackGridList())) {
      wire_list.emplace_back(x - half_wire_width, real_rect.get_ll_y(), x + half_wire_width, real_rect.get_ur_y(), layer_idx);
    }
  }
  return wire_list;
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
    for (Pin& pin : net.get_pin_list()) {
      AccessPoint& access_point = pin.get_access_point();
      PlanarCoord grid_coord = access_point.get_grid_coord();
      int32_t layer_idx = access_point.get_layer_idx();
      gr_net.pin_list.push_back(GRPin{&pin, grid_coord, layer_idx});
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
    MacroPinEdge macro_pin_edge = gr_pin.pin->get_macro_pin_edge();
    bool is_vertical_macro_edge = macro_pin_edge == MacroPinEdge::kNorth || macro_pin_edge == MacroPinEdge::kSouth;
    bool is_horizontal_macro_edge = macro_pin_edge == MacroPinEdge::kEast || macro_pin_edge == MacroPinEdge::kWest;
    bool toward_low = false;
    if ((is_horizontal && is_horizontal_macro_edge) || (!is_horizontal && is_vertical_macro_edge)) {
      // A macro pin escapes from the instance edge it sits on. The nearest gcell
      // boundary may lie inside the macro body and leave the pin without a way out.
      toward_low = macro_pin_edge == MacroPinEdge::kSouth || macro_pin_edge == MacroPinEdge::kWest;
    } else {
      PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(gr_pin.grid_coord, gcell_axis);
      PlanarCoord gcell_mid_coord = gcell_rect.getMidPoint();
      PlanarCoord real_coord = gr_pin.pin->get_access_point().get_real_coord();
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
    }

    int32_t direction = toward_low ? -1 : 1;
    if (is_horizontal) {
      if (direction < 0 && gr_pin.grid_coord.get_x() == 0) {
        direction = 1;
      } else if (direction > 0 && gr_pin.grid_coord.get_x() == _grid_graph.getXSize() - 1) {
        direction = -1;
      }
    } else {
      if (direction < 0 && gr_pin.grid_coord.get_y() == 0) {
        direction = 1;
      } else if (direction > 0 && gr_pin.grid_coord.get_y() == _grid_graph.getYSize() - 1) {
        direction = -1;
      }
    }
    addPinEscapeCorridor(net, gr_pin, is_horizontal, direction);
  }
}

void GlobalRouter::addPinEscapeCorridor(GRNet& net, const GRPin& gr_pin, bool is_horizontal, int32_t direction)
{
  int32_t layer_idx = gr_pin.layer_idx;
  int32_t x = gr_pin.grid_coord.get_x();
  int32_t y = gr_pin.grid_coord.get_y();
  GRWireEdgeId edge_id = getEscapeEdgeId(x, y, is_horizontal, direction, layer_idx);
  if (!_grid_graph.hasWireEdge(edge_id)) {
    return;
  }
  net.exempt_edge_set.insert(edge_id);

  // The enlarged pin shape may still block the neighbor gcell; open at most one more gcell outward.
  int32_t next_x = x + (is_horizontal ? direction : 0);
  int32_t next_y = y + (is_horizontal ? 0 : direction);
  if (next_x < 0 || next_x >= _grid_graph.getXSize() || next_y < 0 || next_y >= _grid_graph.getYSize()) {
    return;
  }
  GRWireEdgeId next_edge_id = getEscapeEdgeId(next_x, next_y, is_horizontal, direction, layer_idx);
  if (!_grid_graph.hasWireEdge(next_edge_id)) {
    return;
  }
  if (_grid_graph.getWireEdge(next_edge_id).getUsableSupply() <= GRParams::kEpsilon) {
    net.exempt_edge_set.insert(next_edge_id);
  }
}

GRWireEdgeId GlobalRouter::getEscapeEdgeId(int32_t x, int32_t y, bool is_horizontal, int32_t direction, int32_t layer_idx) const
{
  if (is_horizontal) {
    return GRWireEdgeId{direction > 0 ? x : x - 1, y, layer_idx};
  }
  return GRWireEdgeId{x, direction > 0 ? y : y - 1, layer_idx};
}

void GlobalRouter::patternRouteGRNetList()
{
  _pattern_failed_net_list.clear();
  _unrouted_net_list.clear();
  if (_gr_net_list.empty()) {
    return;
  }
  size_t next_percent = 10;
  for (size_t i = 0; i < _gr_net_list.size(); i++) {
    GRNet& net = _gr_net_list[i];
    GRTree old_tree = net.routing_tree;
    ripupRoute(net, old_tree);

    GRTree new_tree;
    if (!buildRoutingTree(net, new_tree)) {
      commitRoute(net, old_tree);
      _pattern_failed_net_list.push_back(&net);
    } else {
      commitRoute(net, new_tree);
    }

    size_t percent = ((i + 1) * 100) / _gr_net_list.size();
    if (percent >= next_percent || i + 1 == _gr_net_list.size()) {
      RTLOG.info(Loc::current(), "Routed ", i + 1, "/", _gr_net_list.size(), "(", percent, "%)");
      next_percent += 10;
    }
  }
  RTLOG.info(Loc::current(), "Pattern route failed net num: ", _pattern_failed_net_list.size(), ", left to maze route.");
}

void GlobalRouter::detourRouteGRNetList()
{
  std::vector<std::pair<double, GRNet*>> net_list;
  for (GRNet* net : getOverflowNetList()) {
    double overflow = getRoutingTreeOverflow(*net, net->routing_tree);
    if (overflow > GRParams::kEpsilon) {
      net_list.emplace_back(overflow, net);
    }
  }
  if (net_list.empty()) {
    RTLOG.info(Loc::current(), "Pattern detour: no overflow net");
    return;
  }
  initDetourCongestionView();
  std::sort(net_list.begin(), net_list.end(), [](const std::pair<double, GRNet*>& first, const std::pair<double, GRNet*>& second) {
    return first.first > second.first;
  });
  double before_overflow = getTotalOverflow();
  int32_t accepted_num = 0;
  int32_t rolled_back_num = 0;
  int32_t scaffold_num = 0;
  size_t next_percent = 10;
  for (size_t i = 0; i < net_list.size(); i++) {
    GRNet& net = *(net_list[i].second);
    GRTree old_tree = net.routing_tree;
    double old_overflow = getRoutingTreeOverflow(net, old_tree);
    double old_cost = getRoutingTreeCost(net, old_tree);
    if (old_overflow <= GRParams::kEpsilon) {
      continue;
    }
    _last_detour_shifted = false;
    ripupRoute(net, old_tree);

    GRTree new_tree;
    bool built = buildDetourRoutingTree(net, new_tree);
    if (built && _last_detour_shifted) {
      scaffold_num++;
    }
    if (!built) {
      commitRoute(net, old_tree);
      rolled_back_num++;
    } else {
      commitRoute(net, new_tree);
      double new_overflow = getRoutingTreeOverflow(net, new_tree);
      double new_cost = getRoutingTreeCost(net, new_tree);
      bool accepted = new_overflow < old_overflow - GRParams::kEpsilon || (new_overflow <= old_overflow + GRParams::kEpsilon && new_cost < old_cost - GRParams::kEpsilon);
      if (accepted) {
        accepted_num++;
      } else {
        ripupRoute(net, new_tree);
        commitRoute(net, old_tree);
        rolled_back_num++;
      }
    }

    size_t percent = ((i + 1) * 100) / net_list.size();
    if (percent >= next_percent || i + 1 == net_list.size()) {
      RTLOG.info(Loc::current(), "Pattern detoured ", i + 1, "/", net_list.size(), "(", percent, "%)");
      next_percent += 10;
    }
  }
  double after_overflow = getTotalOverflow();
  RTLOG.info(Loc::current(), "Pattern detour: nets ", net_list.size(), ", overflow ", before_overflow, " -> ", after_overflow, ", overflow_net ",
             getOverflowNetList().size(), ", accepted ", accepted_num, ", rolled back ", rolled_back_num, ", scaffold ", scaffold_num);
}

void GlobalRouter::mazeRouteGRNetList()
{
  if (!_pattern_failed_net_list.empty()) {
    std::vector<GRNet*> failed_net_list = std::move(_pattern_failed_net_list);
    _pattern_failed_net_list.clear();
    RTLOG.info(Loc::current(), "Maze routing pattern-failed nets: ", failed_net_list.size());
    size_t next_percent = 10;
    for (size_t i = 0; i < failed_net_list.size(); i++) {
      if (!routeGRNetByMaze(*failed_net_list[i])) {
        _unrouted_net_list.push_back(failed_net_list[i]);
      }
      size_t percent = ((i + 1) * 100) / failed_net_list.size();
      if (percent >= next_percent || i + 1 == failed_net_list.size()) {
        RTLOG.info(Loc::current(), "Maze routed pattern-failed nets ", i + 1, "/", failed_net_list.size(), "(", percent, "%)");
        next_percent += 10;
      }
    }
    RTLOG.info(Loc::current(), "Unrouted net num: ", _unrouted_net_list.size(), ", left to blocked fallback.");
  }

  constexpr int32_t kMaxIteration = 6;
  for (int32_t iteration = 0; iteration < kMaxIteration; iteration++) {
    std::vector<GRNet*> net_list = getOverflowNetList();
    if (net_list.empty()) {
      break;
    }
    double before_overflow = getTotalOverflow();
    double before_cost = getTotalRoutingCost();
    std::vector<std::pair<GRNet*, GRTree>> snapshot_list;
    snapshot_list.reserve(net_list.size());
    size_t next_percent = 10;
    for (size_t i = 0; i < net_list.size(); i++) {
      GRNet* net = net_list[i];
      snapshot_list.emplace_back(net, net->routing_tree);
      routeGRNetByMaze(*net);
      size_t percent = ((i + 1) * 100) / net_list.size();
      if (percent >= next_percent || i + 1 == net_list.size()) {
        RTLOG.info(Loc::current(), "Maze routed ", i + 1, "/", net_list.size(), "(", percent, "%)");
        next_percent += 10;
      }
    }
    double after_overflow = getTotalOverflow();
    double after_cost = getTotalRoutingCost();
    // Overflow is the primary reroute objective; cost only breaks ties when overflow is unchanged.
    bool accepted = after_overflow < before_overflow - GRParams::kEpsilon || (after_overflow <= before_overflow + GRParams::kEpsilon && after_cost < before_cost - GRParams::kEpsilon);
    RTLOG.info(Loc::current(), "Maze reroute iteration ", iteration, ": nets ", net_list.size(), ", overflow ", before_overflow, " -> ", after_overflow,
               ", cost ", before_cost, " -> ", after_cost, ", cost_weight ", _gr_params.get_cost_weight(), accepted ? " (accepted)" : " (rolled back)");
    if (!accepted) {
      for (auto& [net, old_tree] : snapshot_list) {
        ripupRoute(*net, net->routing_tree);
        commitRoute(*net, old_tree);
      }
      break;
    }
    _gr_params.set_cost_weight(_gr_params.get_cost_weight() * 2.0);
    updateCongestionCost();
  }
  routeUnroutedNetList();
}

void GlobalRouter::routeUnroutedNetList()
{
  if (_unrouted_net_list.empty()) {
    return;
  }
  RTLOG.info(Loc::current(), "Fallback routing ", _unrouted_net_list.size(), " nets that have no maze path.");
  for (GRNet* net : _unrouted_net_list) {
    GRTree old_tree = net->routing_tree;
    ripupRoute(*net, old_tree);
    GRTree new_tree;
    buildDirectConnectTree(*net, new_tree);
    commitRoute(*net, new_tree);
    RTLOG.warn(Loc::current(), "Net ", net->origin_net->get_net_name(), " has no maze path, connected by straight lines ignoring supply.");
  }
  _unrouted_net_list.clear();
}

// 这些 net 已经找不到可用 supply 的路径, 只用最少的直线把 pin 连起来, 保证下游能拿到一条结果
void GlobalRouter::buildDirectConnectTree(const GRNet& net, GRTree& routing_tree)
{
  routing_tree = GRTree();
  if (net.pin_list.empty()) {
    return;
  }
  int32_t horizontal_layer_idx = -1;
  int32_t vertical_layer_idx = -1;
  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    if (_grid_graph.isHorizontalLayer(layer_idx)) {
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

  const GRPin& root_pin = net.pin_list.front();
  for (size_t pin_offset = 1; pin_offset < net.pin_list.size(); pin_offset++) {
    const GRPin& gr_pin = net.pin_list[pin_offset];
    // 先水平再垂直, 最多两条直线
    PlanarCoord corner(gr_pin.grid_coord.get_x(), root_pin.grid_coord.get_y());
    bool has_horizontal_wire = (root_pin.grid_coord != corner);
    bool has_vertical_wire = (corner != gr_pin.grid_coord);
    int32_t root_wire_layer_idx = has_horizontal_wire ? horizontal_layer_idx : vertical_layer_idx;
    int32_t pin_wire_layer_idx = has_vertical_wire ? vertical_layer_idx : horizontal_layer_idx;
    if (!has_horizontal_wire && !has_vertical_wire) {
      // 同一个 gcell, 直接用层间跳转连接两个 pin
      addDirectConnectVia(routing_tree, root_pin.grid_coord, root_pin.layer_idx, gr_pin.layer_idx);
      continue;
    }
    if (has_horizontal_wire) {
      routing_tree.wire_list.push_back(
          GRWireSegment{LayerCoord(root_pin.grid_coord, horizontal_layer_idx), LayerCoord(corner, horizontal_layer_idx)});
    }
    if (has_vertical_wire) {
      routing_tree.wire_list.push_back(
          GRWireSegment{LayerCoord(corner, vertical_layer_idx), LayerCoord(gr_pin.grid_coord, vertical_layer_idx)});
    }
    if (has_horizontal_wire && has_vertical_wire) {
      addDirectConnectVia(routing_tree, corner, horizontal_layer_idx, vertical_layer_idx);
    }
    addDirectConnectVia(routing_tree, root_pin.grid_coord, root_pin.layer_idx, root_wire_layer_idx);
    addDirectConnectVia(routing_tree, gr_pin.grid_coord, gr_pin.layer_idx, pin_wire_layer_idx);
  }
}

void GlobalRouter::addDirectConnectVia(GRTree& routing_tree, const PlanarCoord& coord, int32_t first_layer_idx, int32_t second_layer_idx) const
{
  if (first_layer_idx == second_layer_idx) {
    return;
  }
  routing_tree.via_list.push_back(
      GRViaStack{coord, std::min(first_layer_idx, second_layer_idx), std::max(first_layer_idx, second_layer_idx)});
}

void GlobalRouter::updateCongestionCost()
{
  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
#pragma omp parallel for collapse(2)
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        double total_usage_ratio = 0.0;
        int32_t neighbor_num = 0;
        for (int32_t neighbor_x = std::max(0, x - _gr_params.get_congestion_radius()); neighbor_x <= std::min(edge_map.get_x_size() - 1, x + _gr_params.get_congestion_radius());
             neighbor_x++) {
          for (int32_t neighbor_y = std::max(0, y - _gr_params.get_congestion_radius()); neighbor_y <= std::min(edge_map.get_y_size() - 1, y + _gr_params.get_congestion_radius());
               neighbor_y++) {
            GRWireEdgeId neighbor_id{neighbor_x, neighbor_y, layer_idx};
            if (!_grid_graph.hasWireEdge(neighbor_id)) {
              continue;
            }
            const GRWireEdge& neighbor_edge = _grid_graph.getWireEdge(neighbor_id);
            double neighbor_supply = neighbor_edge.getUsableSupply();
            if (neighbor_supply <= GRParams::kEpsilon) {
              continue;
            }
            total_usage_ratio += neighbor_edge.getDemand() / neighbor_supply;
            neighbor_num++;
          }
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(GRWireEdgeId{x, y, layer_idx});
        double supply = edge.getUsableSupply();
        double usage_ratio = supply <= GRParams::kEpsilon ? 0.0 : edge.getDemand() / supply;
        usage_ratio = std::max(0.0, usage_ratio - _gr_params.get_congestion_threshold());
        double average_usage_ratio = neighbor_num == 0 ? 0.0 : total_usage_ratio / neighbor_num;
        double congestion_ratio = average_usage_ratio + usage_ratio;
        double new_congestion_cost = _gr_params.get_congestion_cost_scale() * _gr_params.get_cost_weight() * congestion_ratio * congestion_ratio;
        edge.congestion_cost = edge.congestion_cost * _gr_params.get_congestion_decay() + new_congestion_cost;
      }
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
  GRMazeField& field = _maze_field;
  field.init(x_size, y_size);
  // 整个 net 的 supply/demand 在 commitRoute 之前不变, 因此代价缓存与树标记按 net 递增 stamp 即可
  field.edge_cost_id++;
  field.tree_stamp_id++;

  std::vector<bool> terminal_connected(terminal_list.size(), false);
  std::vector<PlanarCoord> tree_coord_list;
  field.markTreeCoord(terminal_list.front());
  tree_coord_list.push_back(terminal_list.front());
  int32_t remaining_terminal_num = static_cast<int32_t>(terminal_list.size());
  for (size_t i = 0; i < terminal_list.size(); i++) {
    if (field.isTreeCoord(terminal_list[i])) {
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
          double wire_cost = getMazeWireCost(net, x, y, true);
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
          double wire_cost = getMazeWireCost(net, x - 1, y, true);
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
          double wire_cost = getMazeWireCost(net, x, y, false);
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
          double wire_cost = getMazeWireCost(net, x, y - 1, false);
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
      double next_cost = current_cost + getDirectionCost();
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
      if (field.markTreeCoord(coord)) {
        tree_coord_list.push_back(coord);
      }
    }
    for (size_t i = 0; i < terminal_list.size(); i++) {
      if (!terminal_connected[i] && field.isTreeCoord(terminal_list[i])) {
        terminal_connected[i] = true;
        remaining_terminal_num--;
      }
    }
  }
  return true;
}

void GlobalRouter::GRMazeField::init(int32_t x_size_, int32_t y_size_)
{
  if (x_size == x_size_ && y_size == y_size_) {
    return;
  }
  x_size = x_size_;
  y_size = y_size_;
  int32_t state_num = 2 * x_size * y_size;
  int32_t coord_num = x_size * y_size;
  g.assign(state_num, GRParams::kInf);
  parent.assign(state_num, -1);
  closed.assign(state_num, false);
  touched.clear();
  tree_stamp.assign(coord_num, 0);
  edge_cost.assign(state_num, 0.0);
  edge_cost_stamp.assign(state_num, 0);
}

void GlobalRouter::GRMazeField::clearSearch()
{
  for (int32_t state : touched) {
    g[state] = GRParams::kInf;
    parent[state] = -1;
    closed[state] = false;
  }
  touched.clear();
}

void GlobalRouter::GRMazeField::setCost(int32_t state, double cost, int32_t parent_state)
{
  g[state] = cost;
  parent[state] = parent_state;
  touched.push_back(state);
}

void GlobalRouter::GRMazeField::setClosed(int32_t state)
{
  closed[state] = 1;
  touched.push_back(state);
}

bool GlobalRouter::GRMazeField::markTreeCoord(const PlanarCoord& coord)
{
  int32_t coord_idx = getCoordIdx(coord);
  if (tree_stamp[coord_idx] == tree_stamp_id) {
    return false;
  }
  tree_stamp[coord_idx] = tree_stamp_id;
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

// (x, y, is_horizontal) 唯一确定一条 wire edge: 它的两个端点都会以同样的参数查询, 因此这里做一次缓存
double GlobalRouter::getMazeWireCost(const GRNet& net, int32_t x, int32_t y, bool is_horizontal) const
{
  int32_t state = (y * _grid_graph.getXSize() + x) * 2 + (is_horizontal ? 0 : 1);
  if (_maze_field.edge_cost_stamp[state] != _maze_field.edge_cost_id) {
    _maze_field.edge_cost_stamp[state] = _maze_field.edge_cost_id;
    _maze_field.edge_cost[state] = computeMazeWireCost(net, x, y, is_horizontal);
  }
  return _maze_field.edge_cost[state];
}

double GlobalRouter::computeMazeWireCost(const GRNet& net, int32_t x, int32_t y, bool is_horizontal) const
{
  double total_supply = 0.0;
  double total_demand = 0.0;
  double total_congestion_cost = 0.0;
  int32_t congestion_layer_num = 0;
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
      return _gr_params.get_unit_wire_cost();
    }
    const GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
    has_edge = true;
    total_supply += edge.getUsableSupply();
    total_demand += edge.routing_demand;
    total_congestion_cost += edge.congestion_cost;
    congestion_layer_num++;
  }
  if (!has_edge || total_supply <= GRParams::kEpsilon) {
    return GRParams::kInf;
  }
  double average_congestion_cost = congestion_layer_num == 0 ? 0.0 : total_congestion_cost / congestion_layer_num;
  return _gr_params.get_unit_wire_cost() + _gr_params.get_cost_weight() * getCongestionCost(total_supply, total_demand, 1.0) + average_congestion_cost;
}

double GlobalRouter::getDirectionCost() const
{
  return _gr_params.get_via_base_cost() * (_gr_params.get_cost_weight() / _gr_params.get_initial_cost_weight());
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
  std::set<GRWireEdgeId> wire_edge_set;
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
        wire_edge_set.insert(edge_id);
        if (isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > GRParams::kEpsilon) {
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
        wire_edge_set.insert(edge_id);
        if (isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > GRParams::kEpsilon) {
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
    for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx <= via_stack.top_layer_idx; layer_idx++) {
      if (hasPlanarEdgeOrAP(net, wire_edge_set, via_stack.coord, layer_idx)) {
        continue;
      }
      bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
      if (is_horizontal) {
        for (int32_t edge_x : {via_stack.coord.get_x() - 1, via_stack.coord.get_x()}) {
          GRWireEdgeId edge_id{edge_x, via_stack.coord.get_y(), layer_idx};
          if (!_grid_graph.hasWireEdge(edge_id)) {
            continue;
          }
          if (isExemptWireEdge(net, edge_id)) {
            continue;
          }
          if (_grid_graph.getWireEdge(edge_id).getUsableSupply() <= GRParams::kEpsilon) {
            continue;
          }
          if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > GRParams::kEpsilon) {
            return true;
          }
        }
      } else {
        for (int32_t edge_y : {via_stack.coord.get_y() - 1, via_stack.coord.get_y()}) {
          GRWireEdgeId edge_id{via_stack.coord.get_x(), edge_y, layer_idx};
          if (!_grid_graph.hasWireEdge(edge_id)) {
            continue;
          }
          if (isExemptWireEdge(net, edge_id)) {
            continue;
          }
          if (_grid_graph.getWireEdge(edge_id).getUsableSupply() <= GRParams::kEpsilon) {
            continue;
          }
          if (edge_set.insert(edge_id).second && _grid_graph.getWireEdge(edge_id).getOverflow() > GRParams::kEpsilon) {
            return true;
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
    if (net_cost >= GRParams::kInf) {
      return GRParams::kInf;
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
    if (wire_cost >= GRParams::kInf) {
      return GRParams::kInf;
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
      if (via_cost >= GRParams::kInf) {
        return GRParams::kInf;
      }
      total_cost += via_cost;
    }
  }
  return total_cost;
}

double GlobalRouter::getRoutingTreeOverflow(const GRNet& net, const GRTree& tree) const
{
  double total_overflow = 0.0;
  std::set<GRWireEdgeId> edge_set;
  for (const GRWireSegment& wire_segment : tree.wire_list) {
    int32_t layer_idx = wire_segment.first.get_layer_idx();
    if (layer_idx != wire_segment.second.get_layer_idx()) {
      continue;
    }
    if (wire_segment.first.get_y() == wire_segment.second.get_y()) {
      int32_t y = wire_segment.first.get_y();
      int32_t begin_x = std::min(wire_segment.first.get_x(), wire_segment.second.get_x());
      int32_t end_x = std::max(wire_segment.first.get_x(), wire_segment.second.get_x());
      for (int32_t x = begin_x; x < end_x; x++) {
        GRWireEdgeId edge_id{x, y, layer_idx};
        if (!_grid_graph.hasWireEdge(edge_id) || isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (edge_set.insert(edge_id).second) {
          total_overflow += _grid_graph.getWireEdge(edge_id).getOverflow();
        }
      }
    } else if (wire_segment.first.get_x() == wire_segment.second.get_x()) {
      int32_t x = wire_segment.first.get_x();
      int32_t begin_y = std::min(wire_segment.first.get_y(), wire_segment.second.get_y());
      int32_t end_y = std::max(wire_segment.first.get_y(), wire_segment.second.get_y());
      for (int32_t y = begin_y; y < end_y; y++) {
        GRWireEdgeId edge_id{x, y, layer_idx};
        if (!_grid_graph.hasWireEdge(edge_id) || isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (edge_set.insert(edge_id).second) {
          total_overflow += _grid_graph.getWireEdge(edge_id).getOverflow();
        }
      }
    }
  }
  return total_overflow;
}

bool GlobalRouter::buildRoutingTree(GRNet& net, GRTree& routing_tree)
{
  routing_tree = GRTree();
  GRSkeletonTree skeleton;
  if (!buildSkeletonTree(net, skeleton)) {
    return false;
  }
  if (!buildTopoTree(net, skeleton, false)) {
    return false;
  }
  return runDP(net, routing_tree);
}

bool GlobalRouter::buildDetourRoutingTree(GRNet& net, GRTree& routing_tree)
{
  routing_tree = GRTree();
  GRSkeletonTree skeleton;
  if (!buildSkeletonTree(net, skeleton)) {
    return false;
  }
  GRSkeletonTree detour_skeleton = skeleton;
  _last_detour_shifted = buildDetourSkeleton(skeleton, detour_skeleton);
  if (!buildTopoTree(net, detour_skeleton, true)) {
    return false;
  }
  return runDP(net, routing_tree);
}

bool GlobalRouter::buildSkeletonTree(GRNet& net, GRSkeletonTree& skeleton)
{
  skeleton = GRSkeletonTree();
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
    GRSkeletonNode root_node;
    root_node.coord = coord_list.front();
    skeleton.node_list.push_back(root_node);
    skeleton.root_idx = 0;
    for (GRPin& gr_pin : net.pin_list) {
      GRSkeletonNode& node = skeleton.node_list[0];
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
  if (static_cast<int32_t>(branch_order_list.size()) != branch_num) {
    RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " FLUTE tree is not connected!");
    Flute::free_tree(flute_tree);
    return false;
  }

  std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC> coord_node_map;
  for (int32_t branch_idx : branch_order_list) {
    PlanarCoord coord(flute_tree.branch[branch_idx].x, flute_tree.branch[branch_idx].y);
    int32_t node_idx = -1;
    auto iter = coord_node_map.find(coord);
    if (iter == coord_node_map.end()) {
      GRSkeletonNode node;
      node.coord = coord;
      node_idx = static_cast<int32_t>(skeleton.node_list.size());
      skeleton.node_list.push_back(node);
      coord_node_map[coord] = node_idx;
    } else {
      node_idx = iter->second;
    }
    if (branch_idx == 0) {
      skeleton.root_idx = node_idx;
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
    if (skeleton.node_list[child_node_idx].parent != -1) {
      continue;
    }
    skeleton.node_list[child_node_idx].parent = parent_node_idx;
    skeleton.node_list[parent_node_idx].child_list.push_back(child_node_idx);
  }

  for (GRPin& gr_pin : net.pin_list) {
    auto iter = coord_node_map.find(gr_pin.grid_coord);
    if (iter == coord_node_map.end()) {
      RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " pin coordinate is not in the FLUTE tree!");
      Flute::free_tree(flute_tree);
      return false;
    }
    GRSkeletonNode& node = skeleton.node_list[iter->second];
    node.fixed_low = std::min(node.fixed_low, gr_pin.layer_idx);
    node.fixed_high = std::max(node.fixed_high, gr_pin.layer_idx);
  }
  Flute::free_tree(flute_tree);

  if (skeleton.root_idx < 0) {
    return false;
  }
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(skeleton.node_list.size()); node_idx++) {
    if (node_idx != skeleton.root_idx && skeleton.node_list[node_idx].parent == -1) {
      return false;
    }
  }
  return true;
}

bool GlobalRouter::buildTopoTree(GRNet& net, const GRSkeletonTree& skeleton, bool with_detour)
{
  net.topology = GRTopoTree();
  if (skeleton.node_list.empty() || skeleton.root_idx < 0) {
    return false;
  }
  for (const GRSkeletonNode& skeleton_node : skeleton.node_list) {
    GRTopoNode topo_node;
    topo_node.coord = skeleton_node.coord;
    topo_node.fixed_low = skeleton_node.fixed_low;
    topo_node.fixed_high = skeleton_node.fixed_high;
    net.topology.node_list.push_back(topo_node);
  }
  net.topology.root_idx = skeleton.root_idx;

  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(skeleton.node_list.size()); node_idx++) {
    if (node_idx == skeleton.root_idx) {
      continue;
    }
    int32_t parent_idx = skeleton.node_list[node_idx].parent;
    if (parent_idx < 0) {
      return false;
    }
    const PlanarCoord& parent_coord = skeleton.node_list[parent_idx].coord;
    const PlanarCoord& child_coord = skeleton.node_list[node_idx].coord;
    std::vector<std::vector<PlanarCoord>> path_list;
    buildPatternPathList(parent_coord, child_coord, with_detour, path_list);

    std::vector<int32_t> candidate_list;
    candidate_list.reserve(path_list.size() + 1);
    for (const std::vector<PlanarCoord>& path : path_list) {
      candidate_list.push_back(addPatternPath(net.topology, parent_idx, node_idx, path));
    }
    bool is_collinear = parent_coord.get_x() == child_coord.get_x() || parent_coord.get_y() == child_coord.get_y();
    if (is_collinear) {
      candidate_list.insert(candidate_list.begin(), -1);
    }
    if (candidate_list.empty()) {
      RTLOG.warn(Loc::current(), "Net ", net.origin_net->get_net_name(), " has no pattern candidate!");
      return false;
    }
    addTopoEdge(net.topology, parent_idx, node_idx, std::move(candidate_list));
  }
  return true;
}

void GlobalRouter::buildPatternPathList(const PlanarCoord& parent_coord, const PlanarCoord& child_coord, bool with_detour,
                                        std::vector<std::vector<PlanarCoord>>& path_list)
{
  path_list.clear();
  bool is_collinear = parent_coord.get_x() == child_coord.get_x() || parent_coord.get_y() == child_coord.get_y();
  if (!is_collinear) {
    addPatternPathCandidate(path_list, {PlanarCoord(child_coord.get_x(), parent_coord.get_y())});
    addPatternPathCandidate(path_list, {PlanarCoord(parent_coord.get_x(), child_coord.get_y())});

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
      addPatternPathCandidate(path_list, {PlanarCoord(x, parent_coord.get_y()), PlanarCoord(x, child_coord.get_y())});
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
      addPatternPathCandidate(path_list, {PlanarCoord(parent_coord.get_x(), y), PlanarCoord(child_coord.get_x(), y)});
    }
  } else {
    int32_t max_offset = with_detour ? _gr_params.get_max_detour_offset() : 2;
    for (int32_t offset = 1; offset <= max_offset; offset++) {
      for (int32_t sign : {1, -1}) {
        if (parent_coord.get_y() == child_coord.get_y()) {
          int32_t y = parent_coord.get_y() + sign * offset;
          if (y < 0 || y >= _grid_graph.getYSize()) {
            continue;
          }
          addPatternPathCandidate(path_list, {PlanarCoord(parent_coord.get_x(), y), PlanarCoord(child_coord.get_x(), y)});
        } else {
          int32_t x = parent_coord.get_x() + sign * offset;
          if (x < 0 || x >= _grid_graph.getXSize()) {
            continue;
          }
          addPatternPathCandidate(path_list, {PlanarCoord(x, parent_coord.get_y()), PlanarCoord(x, child_coord.get_y())});
        }
      }
    }
  }

  if (with_detour && getSegmentCongestionNum(parent_coord, child_coord) > 0) {
    addUDetourPathList(parent_coord, child_coord, path_list);
  }
}

void GlobalRouter::addPatternPathCandidate(std::vector<std::vector<PlanarCoord>>& path_list, const std::vector<PlanarCoord>& path) const
{
  constexpr size_t kMaxPatternPathNum = 64;
  if (path.empty() || path_list.size() >= kMaxPatternPathNum) {
    return;
  }
  for (const PlanarCoord& coord : path) {
    if (coord.get_x() < 0 || coord.get_x() >= _grid_graph.getXSize() || coord.get_y() < 0 || coord.get_y() >= _grid_graph.getYSize()) {
      return;
    }
  }
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i - 1] == path[i]) {
      return;
    }
  }
  for (const std::vector<PlanarCoord>& exist_path : path_list) {
    if (exist_path.size() != path.size()) {
      continue;
    }
    bool is_same = true;
    for (size_t i = 0; i < path.size(); i++) {
      if (exist_path[i] != path[i]) {
        is_same = false;
        break;
      }
    }
    if (is_same) {
      return;
    }
  }
  path_list.push_back(path);
}

void GlobalRouter::addUDetourPathList(const PlanarCoord& parent_coord, const PlanarCoord& child_coord,
                                      std::vector<std::vector<PlanarCoord>>& path_list)
{
  int32_t x_min = std::min(parent_coord.get_x(), child_coord.get_x());
  int32_t x_max = std::max(parent_coord.get_x(), child_coord.get_x());
  int32_t y_min = std::min(parent_coord.get_y(), child_coord.get_y());
  int32_t y_max = std::max(parent_coord.get_y(), child_coord.get_y());

  if (parent_coord.get_y() != child_coord.get_y()) {
    for (int32_t offset = 1; offset <= _gr_params.get_max_detour_offset(); offset++) {
      int32_t left_x = x_min - offset;
      if (left_x >= 0) {
        addPatternPathCandidate(path_list, {PlanarCoord(left_x, parent_coord.get_y()), PlanarCoord(left_x, child_coord.get_y())});
      }
      int32_t right_x = x_max + offset;
      if (right_x < _grid_graph.getXSize()) {
        addPatternPathCandidate(path_list, {PlanarCoord(right_x, parent_coord.get_y()), PlanarCoord(right_x, child_coord.get_y())});
      }
    }
  }
  if (parent_coord.get_x() != child_coord.get_x()) {
    for (int32_t offset = 1; offset <= _gr_params.get_max_detour_offset(); offset++) {
      int32_t lower_y = y_min - offset;
      if (lower_y >= 0) {
        addPatternPathCandidate(path_list, {PlanarCoord(parent_coord.get_x(), lower_y), PlanarCoord(child_coord.get_x(), lower_y)});
      }
      int32_t upper_y = y_max + offset;
      if (upper_y < _grid_graph.getYSize()) {
        addPatternPathCandidate(path_list, {PlanarCoord(parent_coord.get_x(), upper_y), PlanarCoord(child_coord.get_x(), upper_y)});
      }
    }
  }
}

bool GlobalRouter::buildDetourSkeleton(const GRSkeletonTree& skeleton, GRSkeletonTree& detour_skeleton) const
{
  detour_skeleton = skeleton;
  int32_t node_num = static_cast<int32_t>(skeleton.node_list.size());
  if (node_num < 3) {
    return false;
  }
  std::vector<std::vector<int32_t>> adjacency(node_num);
  for (int32_t node_idx = 0; node_idx < node_num; node_idx++) {
    for (int32_t child_idx : skeleton.node_list[node_idx].child_list) {
      adjacency[node_idx].push_back(child_idx);
      adjacency[child_idx].push_back(node_idx);
    }
  }

  std::set<PlanarCoord, CmpPlanarCoordByXASC> coord_set;
  for (const GRSkeletonNode& node : skeleton.node_list) {
    coord_set.insert(node.coord);
  }

  std::set<std::pair<int32_t, int32_t>> chain_set;
  double best_score = GRParams::kInf;
  std::vector<int32_t> best_chain;
  int32_t best_offset = 0;
  bool best_horizontal = false;

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
      if (chain.size() < 3 || !isStraightSkeletonChain(skeleton, chain)) {
        continue;
      }
      std::pair<int32_t, int32_t> chain_key(begin_idx, end_idx);
      if (!chain_set.insert(chain_key).second) {
        continue;
      }

      bool has_fixed_node = false;
      for (size_t i = 1; i + 1 < chain.size(); i++) {
        if (skeleton.node_list[chain[i]].fixed_low != INT_MAX) {
          has_fixed_node = true;
          break;
        }
      }
      if (has_fixed_node) {
        continue;
      }

      std::vector<PlanarCoord> chain_coord_list;
      chain_coord_list.reserve(chain.size());
      for (int32_t node_idx : chain) {
        chain_coord_list.push_back(skeleton.node_list[node_idx].coord);
      }
      int32_t original_congestion = getPathCongestionNum(chain_coord_list);
      if (original_congestion <= 0) {
        continue;
      }
      double original_length = getPathLength(chain_coord_list);
      double max_length = original_length + std::max(2.0, original_length * _gr_params.get_max_detour_length_ratio());
      bool is_horizontal = chain_coord_list.front().get_y() == chain_coord_list[1].get_y();

      for (int32_t offset = -_gr_params.get_max_detour_offset(); offset <= _gr_params.get_max_detour_offset(); offset++) {
        if (offset == 0) {
          continue;
        }
        std::vector<PlanarCoord> shifted_coord_list = chain_coord_list;
        std::set<PlanarCoord, CmpPlanarCoordByXASC> shifted_coord_set;
        bool is_valid = true;
        for (size_t i = 1; i + 1 < shifted_coord_list.size(); i++) {
          PlanarCoord shifted_coord = shifted_coord_list[i];
          if (is_horizontal) {
            shifted_coord.set_y(shifted_coord.get_y() + offset);
          } else {
            shifted_coord.set_x(shifted_coord.get_x() + offset);
          }
          if (shifted_coord.get_x() < 0 || shifted_coord.get_x() >= _grid_graph.getXSize() || shifted_coord.get_y() < 0
              || shifted_coord.get_y() >= _grid_graph.getYSize()) {
            is_valid = false;
            break;
          }
          if (coord_set.find(shifted_coord) != coord_set.end() || !shifted_coord_set.insert(shifted_coord).second) {
            is_valid = false;
            break;
          }
          shifted_coord_list[i] = shifted_coord;
        }
        if (!is_valid) {
          continue;
        }
        int32_t shifted_congestion = getPathCongestionNum(shifted_coord_list);
        if (shifted_congestion >= original_congestion) {
          continue;
        }
        double shifted_length = getPathLength(shifted_coord_list);
        if (shifted_length > max_length + GRParams::kEpsilon) {
          continue;
        }
        double score = shifted_congestion * 1000000.0 + shifted_length;
        if (score < best_score) {
          best_score = score;
          best_chain = chain;
          best_offset = offset;
          best_horizontal = is_horizontal;
        }
      }
    }
  }

  if (best_chain.empty()) {
    return false;
  }
  for (size_t i = 1; i + 1 < best_chain.size(); i++) {
    GRSkeletonNode& node = detour_skeleton.node_list[best_chain[i]];
    if (best_horizontal) {
      node.coord.set_y(node.coord.get_y() + best_offset);
    } else {
      node.coord.set_x(node.coord.get_x() + best_offset);
    }
  }
  return true;
}

bool GlobalRouter::isStraightSkeletonChain(const GRSkeletonTree& skeleton, const std::vector<int32_t>& chain) const
{
  if (chain.size() < 2) {
    return false;
  }
  const PlanarCoord& first_coord = skeleton.node_list[chain[0]].coord;
  const PlanarCoord& second_coord = skeleton.node_list[chain[1]].coord;
  bool is_horizontal = first_coord.get_y() == second_coord.get_y();
  int32_t direction = 0;
  for (size_t i = 1; i < chain.size(); i++) {
    const PlanarCoord& prev_coord = skeleton.node_list[chain[i - 1]].coord;
    const PlanarCoord& curr_coord = skeleton.node_list[chain[i]].coord;
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

void GlobalRouter::initDetourCongestionView()
{
  int32_t x_size = _grid_graph.getXSize();
  int32_t y_size = _grid_graph.getYSize();
  GridMap<int32_t> horizontal_map(x_size, y_size, 0);
  GridMap<int32_t> vertical_map(x_size, y_size, 0);
  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        if (edge_map[x][y].getOverflow() <= GRParams::kEpsilon) {
          continue;
        }
        if (_grid_graph.isHorizontalLayer(layer_idx)) {
          horizontal_map[x][y] = 1;
        } else {
          vertical_map[x][y] = 1;
        }
      }
    }
  }

  _horizontal_congestion_prefix.init(x_size + 1, y_size + 1, 0);
  _vertical_congestion_prefix.init(x_size + 1, y_size + 1, 0);
  for (int32_t x = 0; x < x_size; x++) {
    for (int32_t y = 0; y < y_size; y++) {
      _horizontal_congestion_prefix[x + 1][y + 1] = horizontal_map[x][y] + _horizontal_congestion_prefix[x][y + 1]
                                                    + _horizontal_congestion_prefix[x + 1][y] - _horizontal_congestion_prefix[x][y];
      _vertical_congestion_prefix[x + 1][y + 1] = vertical_map[x][y] + _vertical_congestion_prefix[x][y + 1]
                                                  + _vertical_congestion_prefix[x + 1][y] - _vertical_congestion_prefix[x][y];
    }
  }
}

int32_t GlobalRouter::getCongestionPrefixSum(const GridMap<int32_t>& prefix_map, int32_t begin_x, int32_t begin_y, int32_t end_x,
                                             int32_t end_y) const
{
  if (begin_x < 0 || begin_y < 0 || end_x < begin_x || end_y < begin_y || end_x + 1 >= prefix_map.get_x_size()
      || end_y + 1 >= prefix_map.get_y_size()) {
    return 0;
  }
  return prefix_map[end_x + 1][end_y + 1] - prefix_map[begin_x][end_y + 1] - prefix_map[end_x + 1][begin_y] + prefix_map[begin_x][begin_y];
}

int32_t GlobalRouter::getSegmentCongestionNum(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const
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
    return getCongestionPrefixSum(_horizontal_congestion_prefix, begin_x, first_coord.get_y(), end_x, first_coord.get_y());
  }
  if (first_coord.get_x() == second_coord.get_x()) {
    int32_t begin_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t end_y = std::max(first_coord.get_y(), second_coord.get_y()) - 1;
    if (end_y < begin_y) {
      return 0;
    }
    return getCongestionPrefixSum(_vertical_congestion_prefix, first_coord.get_x(), begin_y, first_coord.get_x(), end_y);
  }
  PlanarCoord first_mid(first_coord.get_x(), second_coord.get_y());
  PlanarCoord second_mid(second_coord.get_x(), first_coord.get_y());
  int32_t first_num = getSegmentCongestionNum(first_coord, first_mid) + getSegmentCongestionNum(first_mid, second_coord);
  int32_t second_num = getSegmentCongestionNum(first_coord, second_mid) + getSegmentCongestionNum(second_mid, second_coord);
  return std::min(first_num, second_num);
}

int32_t GlobalRouter::getPathCongestionNum(const std::vector<PlanarCoord>& path) const
{
  int32_t congestion_num = 0;
  for (size_t i = 1; i < path.size(); i++) {
    congestion_num += getSegmentCongestionNum(path[i - 1], path[i]);
  }
  return congestion_num;
}

double GlobalRouter::getPathLength(const std::vector<PlanarCoord>& path) const
{
  double length = 0.0;
  for (size_t i = 1; i < path.size(); i++) {
    length += std::abs(path[i].get_x() - path[i - 1].get_x()) + std::abs(path[i].get_y() - path[i - 1].get_y());
  }
  return length;
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
  double root_cost = GRParams::kInf;
  for (int32_t layer_idx = bottom_layer_idx; layer_idx <= top_layer_idx; layer_idx++) {
    if (state.d[topo_tree.root_idx][layer_idx] < root_cost) {
      root_cost = state.d[topo_tree.root_idx][layer_idx];
      root_layer_idx = layer_idx;
    }
  }
  if (root_layer_idx < 0) {
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
  int32_t layer_num = state.layer_num;

  // 先把所有子节点算完: 临时空间是本 net 共享的, 只要后面不再递归就不会互相覆盖
  for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    if (!state.computed[child_idx]) {
      computeNodeDP(net, state, child_idx);
    }
    for (int32_t bend_idx : node.child_bend_list[child_pos]) {
      if (bend_idx >= 0 && !state.computed[bend_idx]) {
        computeNodeDP(net, state, bend_idx);
      }
    }
  }

  // 临时空间在 GRDpState 上复用, 下标统一按 (child_pos * layer_num + layer_idx) 展开
  state.via_cost.assign(layer_num, 0.0);
  state.phi.assign(layer_num + 1, 0.0);
  state.blocked_prefix.assign(layer_num + 1, 0);
  state.child_g.assign(child_num * layer_num, GRParams::kInf);
  state.child_bend.assign(child_num * layer_num, -1);
  state.child_range_min.assign(child_num * layer_num * layer_num, GRParams::kInf);
  state.child_range_argmin.assign(child_num * layer_num * layer_num, -1);
  state.sum_min.assign(layer_num * layer_num, GRParams::kInf);

  for (int32_t layer_idx = state.bottom_layer_idx; layer_idx < state.top_layer_idx; layer_idx++) {
    state.via_cost[layer_idx] = getViaCost(net, node.coord, layer_idx);
  }
  for (int32_t layer_idx = 0; layer_idx < layer_num; layer_idx++) {
    if (state.via_cost[layer_idx] >= GRParams::kInf) {
      state.phi[layer_idx + 1] = state.phi[layer_idx];
      state.blocked_prefix[layer_idx + 1] = state.blocked_prefix[layer_idx] + 1;
    } else {
      state.phi[layer_idx + 1] = state.phi[layer_idx] + state.via_cost[layer_idx];
      state.blocked_prefix[layer_idx + 1] = state.blocked_prefix[layer_idx];
    }
  }

  for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
    int32_t child_idx = node.child_list[child_pos];
    const GRTopoNode& child_node = state.topo_tree.node_list[child_idx];
    const std::vector<int32_t>& bend_idx_list = node.child_bend_list[child_pos];
    int32_t child_offset = child_pos * layer_num;
    if (bend_idx_list.empty()) {
      for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
        double wire_cost = getWireSegmentCost(net, layer_idx, node.coord, child_node.coord);
        state.child_g[child_offset + layer_idx] = addCost(state.d[child_idx][layer_idx], wire_cost);
      }
    } else {
      for (int32_t layer_idx = state.bottom_layer_idx; layer_idx <= state.top_layer_idx; layer_idx++) {
        double best_cost = GRParams::kInf;
        int32_t best_bend_idx = -1;
        for (int32_t bend_idx : bend_idx_list) {
          double cost = GRParams::kInf;
          if (bend_idx < 0) {
            double wire_cost = getWireSegmentCost(net, layer_idx, node.coord, child_node.coord);
            cost = addCost(state.d[child_idx][layer_idx], wire_cost);
          } else {
            const PlanarCoord& bend_coord = state.topo_tree.node_list[bend_idx].coord;
            double wire_cost = getWireSegmentCost(net, layer_idx, node.coord, bend_coord);
            cost = addCost(state.d[bend_idx][layer_idx], wire_cost);
          }
          if (cost < best_cost) {
            best_cost = cost;
            best_bend_idx = bend_idx;
          }
        }
        state.child_g[child_offset + layer_idx] = best_cost;
        state.child_bend[child_offset + layer_idx] = best_bend_idx;
      }
    }
    // 区间 [begin, end] 内的最小代价与其所在层, 使下面每个输出层都能 O(1) 取到子节点层
    for (int32_t begin_layer = 0; begin_layer < layer_num; begin_layer++) {
      double min_cost = GRParams::kInf;
      int32_t min_layer = -1;
      for (int32_t end_layer = begin_layer; end_layer < layer_num; end_layer++) {
        if (state.child_g[child_offset + end_layer] < min_cost) {
          min_cost = state.child_g[child_offset + end_layer];
          min_layer = end_layer;
        }
        int32_t range_offset = (child_offset + begin_layer) * layer_num + end_layer;
        state.child_range_min[range_offset] = min_cost;
        state.child_range_argmin[range_offset] = min_layer;
      }
    }
  }

  for (int32_t begin_layer = state.bottom_layer_idx; begin_layer <= state.top_layer_idx; begin_layer++) {
    for (int32_t end_layer = begin_layer; end_layer <= state.top_layer_idx; end_layer++) {
      double sum = 0.0;
      bool is_valid = true;
      for (int32_t child_pos = 0; child_pos < child_num; child_pos++) {
        double child_min = state.child_range_min[(child_pos * layer_num + begin_layer) * layer_num + end_layer];
        if (child_min >= GRParams::kInf) {
          is_valid = false;
          break;
        }
        sum += child_min;
      }
      if (is_valid) {
        state.sum_min[begin_layer * layer_num + end_layer] = sum;
      }
    }
  }

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
        if (state.blocked_prefix[end_layer] != state.blocked_prefix[begin_layer]) {
          continue;
        }
        double stack_cost = state.phi[end_layer] - state.phi[begin_layer];
        double total_cost = addCost(stack_cost, state.sum_min[begin_layer * layer_num + end_layer]);
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
      int32_t best_child_layer_idx = state.child_range_argmin[(child_pos * layer_num + best_begin) * layer_num + best_end];
      state.best_child_layer[node_idx][layer_idx][child_pos] = best_child_layer_idx;
      if (best_child_layer_idx >= 0) {
        state.best_child_bend[node_idx][layer_idx][child_pos] = state.child_bend[child_pos * layer_num + best_child_layer_idx];
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
    return GRParams::kInf;
  }
  bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
  double total_cost = 0.0;
  if (first_coord.get_y() == second_coord.get_y()) {
    if (!is_horizontal) {
      return GRParams::kInf;
    }
    int32_t y = first_coord.get_y();
    int32_t begin_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t end_x = std::max(first_coord.get_x(), second_coord.get_x());
    for (int32_t x = begin_x; x < end_x; x++) {
      double edge_cost = getWireEdgeCost(net, GRWireEdgeId{x, y, layer_idx});
      if (edge_cost >= GRParams::kInf) {
        return GRParams::kInf;
      }
      total_cost += edge_cost;
    }
    return total_cost;
  }
  if (first_coord.get_x() == second_coord.get_x()) {
    if (is_horizontal) {
      return GRParams::kInf;
    }
    int32_t x = first_coord.get_x();
    int32_t begin_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t end_y = std::max(first_coord.get_y(), second_coord.get_y());
    for (int32_t y = begin_y; y < end_y; y++) {
      double edge_cost = getWireEdgeCost(net, GRWireEdgeId{x, y, layer_idx});
      if (edge_cost >= GRParams::kInf) {
        return GRParams::kInf;
      }
      total_cost += edge_cost;
    }
    return total_cost;
  }
  RTLOG.warn(Loc::current(), "The wire segment is not horizontal or vertical!");
  return GRParams::kInf;
}

double GlobalRouter::getWireEdgeCost(const GRNet& net, const GRWireEdgeId& edge_id, double amount) const
{
  if (edge_id.layer_idx < _grid_graph.getBottomLayerIdx() || edge_id.layer_idx > _grid_graph.getTopLayerIdx()) {
    return GRParams::kInf;
  }
  if (!_grid_graph.hasWireEdge(edge_id)) {
    return GRParams::kInf;
  }
  if (isExemptWireEdge(net, edge_id)) {
    return amount * _gr_params.get_unit_wire_cost();
  }
  const GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
  double usable_supply = edge.getUsableSupply();
  if (usable_supply <= GRParams::kEpsilon) {
    return GRParams::kInf;
  }
  return amount * (_gr_params.get_unit_wire_cost() + _gr_params.get_cost_weight() * getCongestionCost(usable_supply, edge.routing_demand, amount) + edge.congestion_cost);
}

double GlobalRouter::getViaCost(const GRNet& net, const PlanarCoord& coord, int32_t below_layer_idx) const
{
  if (below_layer_idx < _grid_graph.getBottomLayerIdx() || below_layer_idx >= _grid_graph.getTopLayerIdx()) {
    return GRParams::kInf;
  }
  double total_cost = _gr_params.get_via_base_cost();
  for (int32_t side_layer_idx : {below_layer_idx, below_layer_idx + 1}) {
    if (isAPViaSide(net, coord, side_layer_idx)) {
      continue;
    }
    bool is_horizontal = _grid_graph.isHorizontalLayer(side_layer_idx);
    for (int32_t edge_offset = -1; edge_offset <= 0; edge_offset++) {
      int32_t edge_x = is_horizontal ? coord.get_x() + edge_offset : coord.get_x();
      int32_t edge_y = is_horizontal ? coord.get_y() : coord.get_y() + edge_offset;
      GRWireEdgeId edge_id{edge_x, edge_y, side_layer_idx};
      if (!_grid_graph.hasWireEdge(edge_id)) {
        continue;
      }
      const GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
      if (edge.getUsableSupply() <= GRParams::kEpsilon) {
        continue;
      }
      double edge_cost = getWireEdgeCost(net, edge_id, _gr_params.get_via_demand());
      if (edge_cost >= GRParams::kInf) {
        return GRParams::kInf;
      }
      total_cost += edge_cost;
    }
  }
  return total_cost;
}

double GlobalRouter::getCongestionCost(double supply, double demand, double amount) const
{
  double total_demand = demand + amount;
  if (supply <= GRParams::kEpsilon || total_demand > supply) {
    double overflow_ratio = total_demand - std::max(supply, 0.0) + 1.0;
    double overflow_ratio_sqr = overflow_ratio * overflow_ratio;
    return overflow_ratio_sqr * overflow_ratio_sqr * overflow_ratio_sqr;
  }
  double usage_ratio = total_demand / supply;
  double usage_ratio_sqr = usage_ratio * usage_ratio;
  double unit_cost = usage_ratio_sqr * usage_ratio_sqr;
  double saturation_start_ratio = _gr_params.get_congestion_saturation_ratio();
  if (usage_ratio < saturation_start_ratio) {
    return unit_cost;
  }
  double saturation_ratio = (usage_ratio - saturation_start_ratio) / (1.0 - saturation_start_ratio);
  unit_cost += saturation_ratio * saturation_ratio;
  double hotspot_start_ratio = _gr_params.get_congestion_hotspot_ratio();
  if (usage_ratio < hotspot_start_ratio) {
    return unit_cost;
  }
  double hotspot_ratio = (usage_ratio - hotspot_start_ratio) / (1.0 - hotspot_start_ratio);
  unit_cost += 2.0 * hotspot_ratio * hotspot_ratio;
  return unit_cost;
}

double GlobalRouter::addCost(double first, double second) const
{
  if (first >= GRParams::kInf || second >= GRParams::kInf) {
    return GRParams::kInf;
  }
  double sum = first + second;
  return sum >= GRParams::kInf ? GRParams::kInf : sum;
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
  std::set<GRWireEdgeId> wire_edge_set;
  buildWireEdgeSet(routing_tree, wire_edge_set);
  std::set<GRWireEdgeId> used_edge_set;
  addWireDemand(net, routing_tree, used_edge_set, -1.0);
  for (const GRViaStack& via_stack : routing_tree.via_list) {
    addViaDemand(net, via_stack, wire_edge_set, used_edge_set, -_gr_params.get_via_demand());
  }
}

void GlobalRouter::commitRoute(GRNet& net, const GRTree& routing_tree)
{
  std::set<GRWireEdgeId> wire_edge_set;
  buildWireEdgeSet(routing_tree, wire_edge_set);
  std::set<GRWireEdgeId> used_edge_set;
  addWireDemand(net, routing_tree, used_edge_set, 1.0);
  for (const GRViaStack& via_stack : routing_tree.via_list) {
    addViaDemand(net, via_stack, wire_edge_set, used_edge_set, _gr_params.get_via_demand());
  }
  net.routing_tree = routing_tree;
}

void GlobalRouter::buildWireEdgeSet(const GRTree& routing_tree, std::set<GRWireEdgeId>& wire_edge_set) const
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
        wire_edge_set.insert(GRWireEdgeId{x, y, layer_idx});
      }
    } else if (wire_segment.first.get_x() == wire_segment.second.get_x()) {
      if (is_horizontal) {
        continue;
      }
      int32_t x = wire_segment.first.get_x();
      int32_t begin_y = std::min(wire_segment.first.get_y(), wire_segment.second.get_y());
      int32_t end_y = std::max(wire_segment.first.get_y(), wire_segment.second.get_y());
      for (int32_t y = begin_y; y < end_y; y++) {
        wire_edge_set.insert(GRWireEdgeId{x, y, layer_idx});
      }
    }
  }
}

bool GlobalRouter::hasPlanarEdgeOrAP(const GRNet& net, const std::set<GRWireEdgeId>& wire_edge_set, const PlanarCoord& coord,
                                     int32_t layer_idx) const
{
  if (isAPViaSide(net, coord, layer_idx)) {
    return true;
  }
  if (layer_idx < _grid_graph.getBottomLayerIdx() || layer_idx > _grid_graph.getTopLayerIdx()) {
    return false;
  }
  bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
  GRWireEdgeId first_edge = is_horizontal ? GRWireEdgeId{coord.get_x() - 1, coord.get_y(), layer_idx}
                                          : GRWireEdgeId{coord.get_x(), coord.get_y() - 1, layer_idx};
  GRWireEdgeId second_edge{coord.get_x(), coord.get_y(), layer_idx};
  return wire_edge_set.contains(first_edge) || wire_edge_set.contains(second_edge);
}

void GlobalRouter::addWireDemand(const GRNet& net, const GRTree& routing_tree, std::set<GRWireEdgeId>& used_edge_set, double delta)
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
        if (isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (!used_edge_set.insert(edge_id).second) {
          continue;
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
        edge.routing_demand += delta;
        if (edge.routing_demand < -GRParams::kEpsilon) {
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
        if (isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (!used_edge_set.insert(edge_id).second) {
          continue;
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
        edge.routing_demand += delta;
        if (edge.routing_demand < -GRParams::kEpsilon) {
          RTLOG.error(Loc::current(), "The wire edge routing demand is negative!");
        }
        if (edge.routing_demand < 0.0) {
          edge.routing_demand = 0.0;
        }
      }
    }
  }
}

void GlobalRouter::addViaDemand(const GRNet& net, const GRViaStack& via_stack, const std::set<GRWireEdgeId>& wire_edge_set,
                                std::set<GRWireEdgeId>& used_edge_set, double delta)
{
  if (via_stack.bottom_layer_idx < _grid_graph.getBottomLayerIdx() || via_stack.top_layer_idx > _grid_graph.getTopLayerIdx()
      || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
    return;
  }
  for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx <= via_stack.top_layer_idx; layer_idx++) {
    if (hasPlanarEdgeOrAP(net, wire_edge_set, via_stack.coord, layer_idx)) {
      continue;
    }
    bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
    if (is_horizontal) {
      for (int32_t edge_x : {via_stack.coord.get_x() - 1, via_stack.coord.get_x()}) {
        int32_t edge_y = via_stack.coord.get_y();
        GRWireEdgeId edge_id{edge_x, edge_y, layer_idx};
        if (!_grid_graph.hasWireEdge(edge_id)) {
          continue;
        }
        if (isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (_grid_graph.getWireEdge(edge_id).getUsableSupply() <= GRParams::kEpsilon) {
          continue;
        }
        if (!used_edge_set.insert(edge_id).second) {
          continue;
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
        edge.routing_demand += delta;
        if (edge.routing_demand < -GRParams::kEpsilon) {
          RTLOG.error(Loc::current(), "The via projection routing demand is negative!");
        }
        if (edge.routing_demand < 0.0) {
          edge.routing_demand = 0.0;
        }
      }
    } else {
      for (int32_t edge_y : {via_stack.coord.get_y() - 1, via_stack.coord.get_y()}) {
        int32_t edge_x = via_stack.coord.get_x();
        GRWireEdgeId edge_id{edge_x, edge_y, layer_idx};
        if (!_grid_graph.hasWireEdge(edge_id)) {
          continue;
        }
        if (isExemptWireEdge(net, edge_id)) {
          continue;
        }
        if (_grid_graph.getWireEdge(edge_id).getUsableSupply() <= GRParams::kEpsilon) {
          continue;
        }
        if (!used_edge_set.insert(edge_id).second) {
          continue;
        }
        GRWireEdge& edge = _grid_graph.getWireEdge(edge_id);
        edge.routing_demand += delta;
        if (edge.routing_demand < -GRParams::kEpsilon) {
          RTLOG.error(Loc::current(), "The via projection routing demand is negative!");
        }
        if (edge.routing_demand < 0.0) {
          edge.routing_demand = 0.0;
        }
      }
    }
  }
}

void GlobalRouter::outputGlobalResult()
{
  for (GRNet& net : _gr_net_list) {
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
}

void GlobalRouter::printSummary()
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  double total_supply = 0.0;
  double total_demand = 0.0;
  double total_overflow = 0.0;
  double max_overflow = 0.0;
  int32_t overflow_edge_num = 0;

  fort::char_table summary_table;
  summary_table.set_cell_text_align(fort::text_align::right);
  summary_table << fort::header << "routing"
                << "supply"
                << "demand"
                << "overflow"
                << "max_overflow"
                << "overflow_edge" << fort::endr;

  for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
    double layer_supply = 0.0;
    double layer_demand = 0.0;
    double layer_overflow = 0.0;
    double layer_max_overflow = 0.0;
    int32_t layer_overflow_edge_num = 0;
    const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        const GRWireEdge& edge = edge_map[x][y];
        layer_supply += edge.getUsableSupply();
        layer_demand += edge.getDemand();
        double overflow = edge.getOverflow();
        layer_overflow += overflow;
        layer_max_overflow = std::max(layer_max_overflow, overflow);
        if (overflow > 0.0) {
          layer_overflow_edge_num++;
        }
      }
    }
    summary_table << routing_layer_list[layer_idx].get_layer_name() << layer_supply << layer_demand << layer_overflow << layer_max_overflow
                  << layer_overflow_edge_num << fort::endr;
    total_supply += layer_supply;
    total_demand += layer_demand;
    total_overflow += layer_overflow;
    max_overflow = std::max(max_overflow, layer_max_overflow);
    overflow_edge_num += layer_overflow_edge_num;
  }
  summary_table << fort::header << "Total" << total_supply << total_demand << total_overflow << max_overflow << overflow_edge_num << fort::endr;

  RTUTIL.printTableList({summary_table});
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

  // edge_info
  {
    GPStruct edge_info_struct("edge_info");
    for (int32_t layer_idx = _grid_graph.getBottomLayerIdx(); layer_idx <= _grid_graph.getTopLayerIdx(); layer_idx++) {
      const GridMap<GRWireEdge>& edge_map = _grid_graph.getEdgeMap(layer_idx);
      bool is_horizontal = _grid_graph.isHorizontalLayer(layer_idx);
      int32_t info_data_type = static_cast<int32_t>(is_horizontal ? GPDataType::kHEdgeInfo : GPDataType::kVEdgeInfo);
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
          const GRWireEdge& edge = edge_map[x][y];
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect first_gcell_rect = RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis);
          PlanarRect second_gcell_rect = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis);
          PlanarCoord first_coord = first_gcell_rect.getMidPoint();
          PlanarCoord second_coord = second_gcell_rect.getMidPoint();
          PlanarRect edge_rect = is_horizontal
                                     ? PlanarRect(first_coord.get_x(), first_gcell_rect.get_ll_y(), second_coord.get_x(), first_gcell_rect.get_ur_y())
                                     : PlanarRect(first_gcell_rect.get_ll_x(), first_coord.get_y(), first_gcell_rect.get_ur_x(), second_coord.get_y());

          GPBoundary gp_boundary;
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_boundary.set_data_type(info_data_type);
          gp_boundary.set_rect(edge_rect);
          edge_info_struct.push(gp_boundary);

          std::vector<std::string> info_line_list;
          info_line_list.push_back(RTUTIL.getString(is_horizontal ? "H" : "V", " (", x, ",", y, ")-(", second_grid_coord.get_x(), ",",
                                                    second_grid_coord.get_y(), ")"));
          info_line_list.push_back(RTUTIL.getString("overflow: ", edge.getOverflow(), ", supply: ", edge.getUsableSupply(),
                                                    ", demand: ", edge.getDemand(), ", congestion_cost: ", edge.congestion_cost));
          info_line_list.push_back(RTUTIL.getString("fixed_demand: ", edge.fixed_demand));
          int32_t line_spacing = std::max(1, edge_rect.getYSpan() / 8);
          int32_t text_y = edge_rect.get_ur_y();
          for (const std::string& info_line : info_line_list) {
            text_y -= line_spacing;
            GPText gp_text;
            gp_text.set_coord(edge_rect.get_ll_x(), text_y);
            gp_text.set_text_type(info_data_type);
            gp_text.set_message(info_line);
            gp_text.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
            gp_text.set_presentation(GPTextPresentation::kLeftMiddle);
            edge_info_struct.push(gp_text);
          }
        }
      }
    }
    gp_gds.addStruct(edge_info_struct);
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
          if (edge.getOverflow() <= GRParams::kEpsilon) {
            continue;
          }
          PlanarRect first_gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis);
          PlanarCoord first_coord = first_gcell_rect.getMidPoint();
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarCoord second_coord = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis).getMidPoint();

          GPPath gp_path;
          gp_path.set_data_type(static_cast<int32_t>(GPDataType::kOverflow));
          gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_path.set_width(point_size);
          gp_path.set_segment(first_coord, second_coord);
          overflow_struct.push(gp_path);
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
