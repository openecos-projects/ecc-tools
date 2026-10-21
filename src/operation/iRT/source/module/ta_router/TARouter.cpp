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
#include "TARouter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Logger.hpp"
#include "Monitor.hpp"
#include "RoutingLayer.hpp"
#include "Utility.hpp"
#include "ViaMaster.hpp"

namespace irt {

TARouter* TARouter::_ta_router_instance = nullptr;

void TARouter::initInst()
{
  if (_ta_router_instance == nullptr) {
    _ta_router_instance = new TARouter();
  }
}

TARouter& TARouter::getInst()
{
  if (_ta_router_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ta_router_instance;
}

void TARouter::destroyInst()
{
  if (_ta_router_instance != nullptr) {
    delete _ta_router_instance;
    _ta_router_instance = nullptr;
  }
}

void TARouter::route()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  Database& database = RTDM.getDatabase();
  resetWarningCounters();
  _track_usage.clear();
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& detailed_result_map = database.get_net_detailed_result_map();
  detailed_result_map.clear();

  std::vector<Net>& net_list = database.get_net_list();
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& global_result_map = database.get_net_global_result_map();

  int32_t routed_net_num = 0;
  int32_t total_net_num = static_cast<int32_t>(global_result_map.size());
  int32_t next_percent = 10;
  _ta_net_list.clear();
  for (Net& net : net_list) {
    if (net.get_pin_list().size() < 2) {
      continue;
    }
    auto global_iter = global_result_map.find(net.get_net_idx());
    if (global_iter == global_result_map.end() || global_iter->second.empty()) {
      continue;
    }
    TANet ta_net = buildTANet(net);
    if (ta_net.edge_list.empty()) {
      continue;
    }
    _ta_net_list.push_back(std::move(ta_net));
    routed_net_num++;
    if (total_net_num > 0 && routed_net_num * 100 >= total_net_num * next_percent) {
      RTLOG.info(Loc::current(), "Built ", routed_net_num, "/", total_net_num, " nets");
      next_percent += 10;
    }
  }

  for (TANet& ta_net : _ta_net_list) {
    assignTracks(ta_net);
  }
  rerouteOverflowNets();

  for (TANet& ta_net : _ta_net_list) {
    outputNet(ta_net);
  }

  reportWarningSummary();
  RTLOG.info(Loc::current(), "Assigned ", routed_net_num, " nets with ", detailed_result_map.size(), " detailed results");
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void TARouter::resetWarningCounters()
{
  _pin_not_on_gr_net_set.clear();
  _obstacle_overlap_net_set.clear();
  _fallback_net_set.clear();
  _no_free_track_count = 0;
}

void TARouter::reportWarningSummary() const
{
  int32_t total = static_cast<int32_t>(_pin_not_on_gr_net_set.size() + _obstacle_overlap_net_set.size() + _fallback_net_set.size()
                                       + _no_free_track_count);
  if (total == 0) {
    return;
  }
  RTLOG.warn(Loc::current(), "TA warnings: pin_not_on_gr_nets=", _pin_not_on_gr_net_set.size(),
             ", obstacle_overlap_nets=", _obstacle_overlap_net_set.size(), ", fallback_nets=", _fallback_net_set.size(),
             ", no_free_track_edges=", _no_free_track_count, ".");
}

TARouter::TANet TARouter::buildTANet(Net& net)
{
  TANet ta_net;
  ta_net.origin_net = &net;
  ta_net.net_idx = net.get_net_idx();

  std::vector<Segment<LayerCoord>>& global_result_list = RTDM.getDatabase().get_net_global_result_map()[net.get_net_idx()];
  if (global_result_list.empty()) {
    return ta_net;
  }

  // Normalize the GR tree before track assignment:
  // 1. split overlapping / crossing segments at key points;
  // 2. merge collinear degree-2 nodes into one macro edge;
  // 3. keep pins as key nodes so a pin in the middle of a segment is not merged away.
  std::vector<Segment<LayerCoord>> segment_list = global_result_list;
  std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
  std::vector<LayerCoord> candidate_root_coord_list;
  std::set<LayerCoord, CmpLayerCoordByXASC> root_key_set;
  for (Pin& pin : net.get_pin_list()) {
    AccessPoint& access_point = pin.get_access_point();
    if (access_point.get_real_coord() == PlanarCoord(-1, -1)) {
      continue;
    }
    LayerCoord key(access_point.get_grid_coord(), access_point.get_layer_idx());
    if (!isCoordCoveredBySegments(key, segment_list)) {
      _pin_not_on_gr_net_set.insert(ta_net.net_idx);
      continue;
    }
    key_coord_pin_map[key].insert(pin.get_pin_idx());
    if (root_key_set.insert(key).second) {
      candidate_root_coord_list.push_back(key);
    }
  }
  if (candidate_root_coord_list.empty()) {
    candidate_root_coord_list.push_back(segment_list.front().get_first());
  }

  MTree<LayerCoord> coord_tree = RTUTIL.getTreeByFullFlow(candidate_root_coord_list, segment_list, key_coord_pin_map);
  std::vector<Segment<TNode<LayerCoord>*>> tree_segment_list = RTUTIL.getSegListByTree(coord_tree);
  std::vector<Segment<LayerCoord>> normalized_segment_list;
  normalized_segment_list.reserve(tree_segment_list.size());
  for (Segment<TNode<LayerCoord>*>& tree_segment : tree_segment_list) {
    normalized_segment_list.emplace_back(tree_segment.get_first()->value(), tree_segment.get_second()->value());
  }

  NodeMap node_map;
  buildTANetFromSegments(ta_net, normalized_segment_list, node_map);
  attachPins(ta_net, node_map);
  return ta_net;
}

bool TARouter::isCoordCoveredBySegments(const LayerCoord& key, const std::vector<Segment<LayerCoord>>& segment_list) const
{
  for (const Segment<LayerCoord>& segment : segment_list) {
    const LayerCoord& first_coord = segment.get_first();
    const LayerCoord& second_coord = segment.get_second();
    int32_t first_layer_idx = first_coord.get_layer_idx();
    int32_t second_layer_idx = second_coord.get_layer_idx();
    if (first_layer_idx == second_layer_idx) {
      if (key.get_layer_idx() != first_layer_idx) {
        continue;
      }
      const PlanarCoord& key_coord = key.get_planar_coord();
      const PlanarCoord& first_planar_coord = first_coord.get_planar_coord();
      const PlanarCoord& second_planar_coord = second_coord.get_planar_coord();
      if (key_coord == first_planar_coord || key_coord == second_planar_coord) {
        return true;
      }
      if (first_coord.get_y() == second_coord.get_y() && key.get_y() == first_coord.get_y()
          && std::min(first_coord.get_x(), second_coord.get_x()) <= key.get_x()
          && key.get_x() <= std::max(first_coord.get_x(), second_coord.get_x())) {
        return true;
      }
      if (first_coord.get_x() == second_coord.get_x() && key.get_x() == first_coord.get_x()
          && std::min(first_coord.get_y(), second_coord.get_y()) <= key.get_y()
          && key.get_y() <= std::max(first_coord.get_y(), second_coord.get_y())) {
        return true;
      }
      continue;
    }

    int32_t bottom_layer_idx = std::min(first_layer_idx, second_layer_idx);
    int32_t top_layer_idx = std::max(first_layer_idx, second_layer_idx);
    if (key.get_layer_idx() < bottom_layer_idx || key.get_layer_idx() > top_layer_idx) {
      continue;
    }
    if (key.get_planar_coord() == first_coord.get_planar_coord() && key.get_planar_coord() == second_coord.get_planar_coord()) {
      return true;
    }
  }
  return false;
}

void TARouter::buildTANetFromSegments(TANet& ta_net, const std::vector<Segment<LayerCoord>>& segment_list, NodeMap& node_map)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  for (const Segment<LayerCoord>& segment : segment_list) {
    const LayerCoord& first_coord = segment.get_first();
    const LayerCoord& second_coord = segment.get_second();
    if (first_coord.get_layer_idx() == second_coord.get_layer_idx()) {
      if (first_coord.get_planar_coord() == second_coord.get_planar_coord()) {
        continue;
      }
      int32_t first_node_idx = getOrCreateNode(ta_net, node_map, first_coord.get_x(), first_coord.get_y(), first_coord.get_layer_idx());
      int32_t second_node_idx = getOrCreateNode(ta_net, node_map, second_coord.get_x(), second_coord.get_y(), second_coord.get_layer_idx());
      PlanarRect guide_rect = RTUTIL.getRealRectByGCell(first_coord, second_coord, gcell_axis);
      addEdge(ta_net, first_node_idx, second_node_idx, first_coord.get_layer_idx(), guide_rect, false);
      continue;
    }

    int32_t first_layer_idx = first_coord.get_layer_idx();
    int32_t second_layer_idx = second_coord.get_layer_idx();
    RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
    PlanarRect guide_rect = RTUTIL.getRealRectByGCell(first_coord.get_planar_coord(), second_coord.get_planar_coord(), gcell_axis);
    if (first_coord.get_planar_coord() != second_coord.get_planar_coord()) {
      // A valid via stack must be vertical at one planar coordinate.
      guide_rect = RTUTIL.getRealRectByGCell(first_coord.get_planar_coord(), gcell_axis);
    }
    int32_t previous_node_idx = -1;
    for (int32_t layer_idx = first_layer_idx; layer_idx <= second_layer_idx; layer_idx++) {
      int32_t node_idx = getOrCreateNode(ta_net, node_map, first_coord.get_x(), first_coord.get_y(), layer_idx);
      if (previous_node_idx != -1) {
        addEdge(ta_net, previous_node_idx, node_idx, layer_idx - 1, guide_rect, true);
      }
      previous_node_idx = node_idx;
    }
  }
}

int32_t TARouter::getOrCreateNode(TANet& ta_net, NodeMap& node_map, int32_t x, int32_t y, int32_t layer_idx)
{
  std::tuple<int32_t, int32_t, int32_t> key(x, y, layer_idx);
  auto iter = node_map.find(key);
  if (iter != node_map.end()) {
    return iter->second;
  }

  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis);

  TANode ta_node;
  ta_node.grid_coord = PlanarCoord(x, y);
  ta_node.real_coord = LayerCoord(gcell_rect.getMidPoint(), layer_idx);
  ta_node.layer_idx = layer_idx;

  int32_t node_idx = static_cast<int32_t>(ta_net.node_list.size());
  ta_net.node_list.push_back(ta_node);
  node_map[key] = node_idx;
  return node_idx;
}

void TARouter::addEdge(TANet& ta_net, int32_t first_node_idx, int32_t second_node_idx, int32_t layer_idx, const PlanarRect& guide_rect, bool is_via)
{
  TAEdge edge;
  edge.first_node_idx = first_node_idx;
  edge.second_node_idx = second_node_idx;
  edge.layer_idx = layer_idx;
  edge.guide_rect = guide_rect;
  edge.is_via = is_via;
  int32_t edge_idx = static_cast<int32_t>(ta_net.edge_list.size());
  ta_net.edge_list.push_back(edge);
  ta_net.node_list[first_node_idx].edge_idx_list.push_back(edge_idx);
  ta_net.node_list[second_node_idx].edge_idx_list.push_back(edge_idx);
}

void TARouter::attachPins(TANet& ta_net, NodeMap& node_map)
{
  for (Pin& pin : ta_net.origin_net->get_pin_list()) {
    attachPin(ta_net, node_map, pin);
  }
}

void TARouter::attachPin(TANet& ta_net, NodeMap& node_map, Pin& pin)
{
  AccessPoint& access_point = pin.get_access_point();
  if (access_point.get_real_coord() == PlanarCoord(-1, -1)) {
    return;
  }
  PlanarCoord ap_grid_coord = access_point.get_grid_coord();
  int32_t ap_layer_idx = access_point.get_layer_idx();
  std::tuple<int32_t, int32_t, int32_t> key(ap_grid_coord.get_x(), ap_grid_coord.get_y(), ap_layer_idx);
  auto iter = node_map.find(key);
  if (iter == node_map.end()) {
    return;
  }
  // Pin is a gcell terminal. TA stops just inside the gcell boundary; DR connects the AP.
  ta_net.node_list[iter->second].is_pin = true;
  ta_net.node_list[iter->second].pin_ap_list.push_back(access_point.get_real_coord());
}

int32_t TARouter::getPinTerminalScale(const TANet& ta_net, int32_t node_idx, int32_t other_node_idx, bool is_horizontal) const
{
  const TANode& ta_node = ta_net.node_list[node_idx];
  const TANode& other_node = ta_net.node_list[other_node_idx];
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  if (ta_node.layer_idx < 0 || ta_node.layer_idx >= static_cast<int32_t>(routing_layer_list.size())) {
    return 0;
  }
  RoutingLayer& routing_layer = routing_layer_list[ta_node.layer_idx];
  PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(ta_node.grid_coord, RTDM.getDatabase().get_gcell_axis());
  int32_t margin = std::max(1, routing_layer.get_min_width());

  int32_t low = 0;
  int32_t high = 0;
  bool toward_high = false;
  if (is_horizontal) {
    low = gcell_rect.get_ll_x();
    high = gcell_rect.get_ur_x();
    toward_high = other_node.grid_coord.get_x() > ta_node.grid_coord.get_x();
  } else {
    low = gcell_rect.get_ll_y();
    high = gcell_rect.get_ur_y();
    toward_high = other_node.grid_coord.get_y() > ta_node.grid_coord.get_y();
  }
  if (high - low <= 2 * margin) {
    return (low + high) / 2;
  }
  return toward_high ? high - margin : low + margin;
}

int32_t TARouter::getPinTrackTarget(const TANet& ta_net, const TAEdge& edge, bool is_horizontal) const
{
  int64_t scale_sum = 0;
  int32_t ap_num = 0;
  for (int32_t node_idx : {edge.first_node_idx, edge.second_node_idx}) {
    const TANode& ta_node = ta_net.node_list[node_idx];
    if (!ta_node.is_pin) {
      continue;
    }
    for (const PlanarCoord& ap_coord : ta_node.pin_ap_list) {
      scale_sum += is_horizontal ? ap_coord.get_y() : ap_coord.get_x();
      ap_num++;
    }
  }
  if (ap_num == 0) {
    return -1;
  }
  return static_cast<int32_t>(scale_sum / ap_num);
}

void TARouter::resolveNodeAndViaCoords(TANet& ta_net)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  // Pin node is a gcell terminal: keep it on the selected track, just inside
  // the gcell boundary facing its neighbor. DR connects the AP to this point.
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(ta_net.node_list.size()); node_idx++) {
    TANode& ta_node = ta_net.node_list[node_idx];
    if (!ta_node.is_pin) {
      continue;
    }
    for (int32_t edge_idx : ta_node.edge_idx_list) {
      const TAEdge& edge = ta_net.edge_list[edge_idx];
      if (edge.is_via) {
        continue;
      }
      int32_t other_node_idx = (edge.first_node_idx == node_idx) ? edge.second_node_idx : edge.first_node_idx;
      if (isHorizontalEdge(ta_net, edge)) {
        ta_node.real_coord = LayerCoord(getPinTerminalScale(ta_net, node_idx, other_node_idx, true), edge.track_coord, ta_node.layer_idx);
      } else {
        ta_node.real_coord = LayerCoord(edge.track_coord, getPinTerminalScale(ta_net, node_idx, other_node_idx, false), ta_node.layer_idx);
      }
      break;
    }
  }

  // Non-pin node coordinate comes from its incident H/V tracks. The along-track
  // coordinate stays at the gcell center, which is also the GR segment endpoint.
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(ta_net.node_list.size()); node_idx++) {
    TANode& ta_node = ta_net.node_list[node_idx];
    if (ta_node.is_pin) {
      continue;
    }
    int32_t h_track = -1;
    int32_t v_track = -1;
    for (int32_t edge_idx : ta_node.edge_idx_list) {
      const TAEdge& edge = ta_net.edge_list[edge_idx];
      if (edge.is_via) {
        continue;
      }
      if (isHorizontalEdge(ta_net, edge)) {
        h_track = edge.track_coord;
      } else {
        v_track = edge.track_coord;
      }
    }
    if (h_track == -1 && v_track == -1) {
      continue;
    }
    PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(ta_node.grid_coord, gcell_axis);
    int32_t x = (v_track != -1) ? v_track : gcell_rect.getMidPoint().get_x();
    int32_t y = (h_track != -1) ? h_track : gcell_rect.getMidPoint().get_y();
    ta_node.real_coord = LayerCoord(x, y, ta_node.layer_idx);
  }

  // Resolve one planar point per via stack, not per via edge. A via stack may
  // contain several adjacent-layer edges; resolving them independently lets a
  // lower edge overwrite the coordinate chosen by an upper edge.
  std::vector<bool> via_edge_visited(ta_net.edge_list.size(), false);
  for (int32_t start_edge_idx = 0; start_edge_idx < static_cast<int32_t>(ta_net.edge_list.size()); start_edge_idx++) {
    if (via_edge_visited[start_edge_idx] || !ta_net.edge_list[start_edge_idx].is_via) {
      continue;
    }
    std::vector<int32_t> stack_node_idx_list;
    std::vector<bool> node_in_stack(ta_net.node_list.size(), false);
    std::vector<int32_t> edge_stack;
    edge_stack.push_back(start_edge_idx);
    via_edge_visited[start_edge_idx] = true;
    while (!edge_stack.empty()) {
      int32_t edge_idx = edge_stack.back();
      edge_stack.pop_back();
      const TAEdge& via_edge = ta_net.edge_list[edge_idx];
      for (int32_t node_idx : {via_edge.first_node_idx, via_edge.second_node_idx}) {
        if (!node_in_stack[node_idx]) {
          node_in_stack[node_idx] = true;
          stack_node_idx_list.push_back(node_idx);
        }
        for (int32_t next_edge_idx : ta_net.node_list[node_idx].edge_idx_list) {
          if (!ta_net.edge_list[next_edge_idx].is_via || via_edge_visited[next_edge_idx]) {
            continue;
          }
          via_edge_visited[next_edge_idx] = true;
          edge_stack.push_back(next_edge_idx);
        }
      }
    }

    int32_t h_track = -1;
    int32_t v_track = -1;
    for (int32_t node_idx : stack_node_idx_list) {
      for (int32_t edge_idx : ta_net.node_list[node_idx].edge_idx_list) {
        const TAEdge& wire_edge = ta_net.edge_list[edge_idx];
        if (wire_edge.is_via) {
          continue;
        }
        if (isHorizontalEdge(ta_net, wire_edge)) {
          if (h_track == -1) {
            h_track = wire_edge.track_coord;
          }
        } else {
          if (v_track == -1) {
            v_track = wire_edge.track_coord;
          }
        }
      }
    }

    PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(ta_net.node_list[stack_node_idx_list.front()].grid_coord, gcell_axis);
    int32_t x = (v_track != -1) ? v_track : gcell_rect.getMidPoint().get_x();
    int32_t y = (h_track != -1) ? h_track : gcell_rect.getMidPoint().get_y();
    for (int32_t node_idx : stack_node_idx_list) {
      TANode& ta_node = ta_net.node_list[node_idx];
      ta_node.real_coord = LayerCoord(x, y, ta_node.layer_idx);
    }
  }
}

void TARouter::assignTracks(TANet& ta_net)
{
  for (TAEdge& edge : ta_net.edge_list) {
    if (edge.is_via) {
      continue;
    }
    assignWireTrack(ta_net, edge);
  }
  resolveNodeAndViaCoords(ta_net);
  commitNetUsage(ta_net);
  int32_t obstacle_overlap_num = countObstacleOverlap(ta_net);
  if (obstacle_overlap_num > 0) {
    _obstacle_overlap_net_set.insert(ta_net.net_idx);
  }
}

int32_t TARouter::countObstacleOverlap(const TANet& ta_net) const
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t overlap_num = 0;
  for (const TAEdge& edge : ta_net.edge_list) {
    if (edge.is_via) {
      continue;
    }
    const TANode& first_node = ta_net.node_list[edge.first_node_idx];
    const TANode& second_node = ta_net.node_list[edge.second_node_idx];
    bool is_horizontal = first_node.grid_coord.get_y() == second_node.grid_coord.get_y() && first_node.grid_coord.get_x() != second_node.grid_coord.get_x();
    bool is_vertical = first_node.grid_coord.get_x() == second_node.grid_coord.get_x() && first_node.grid_coord.get_y() != second_node.grid_coord.get_y();
    if (!is_horizontal && !is_vertical) {
      continue;
    }
    if (edge.layer_idx < 0 || edge.layer_idx >= static_cast<int32_t>(routing_layer_list.size())) {
      continue;
    }
    int32_t half_width = routing_layer_list[edge.layer_idx].get_min_width() / 2;
    int32_t begin_scale = is_horizontal ? std::min(first_node.real_coord.get_x(), second_node.real_coord.get_x())
                                        : std::min(first_node.real_coord.get_y(), second_node.real_coord.get_y());
    int32_t end_scale = is_horizontal ? std::max(first_node.real_coord.get_x(), second_node.real_coord.get_x())
                                      : std::max(first_node.real_coord.get_y(), second_node.real_coord.get_y());
    PlanarRect wire_rect;
    if (is_horizontal) {
      wire_rect.set_ll(begin_scale, edge.track_coord - half_width);
      wire_rect.set_ur(end_scale, edge.track_coord + half_width);
    } else {
      wire_rect.set_ll(edge.track_coord - half_width, begin_scale);
      wire_rect.set_ur(edge.track_coord + half_width, end_scale);
    }
    EXTLayerRect query_rect;
    query_rect.set_real_rect(edge.guide_rect);
    query_rect.set_layer_idx(edge.layer_idx);
    std::map<int32_t, std::set<EXTLayerRect*>> fixed_rect_map = RTDM.getNetFixedRectMap(true, query_rect);
    bool has_obstacle = false;
    auto obstacle_iter = fixed_rect_map.find(-1);
    if (obstacle_iter != fixed_rect_map.end()) {
      for (EXTLayerRect* fixed_rect : obstacle_iter->second) {
        if (RTUTIL.isOpenOverlap(wire_rect, fixed_rect->get_real_rect())) {
          has_obstacle = true;
          break;
        }
      }
    }
    if (has_obstacle) {
      overlap_num++;
    }
  }
  return overlap_num;
}

void TARouter::commitNetUsage(TANet& ta_net)
{
  for (const TAEdge& edge : ta_net.edge_list) {
    if (edge.is_via) {
      continue;
    }
    TANode& first_node = ta_net.node_list[edge.first_node_idx];
    TANode& second_node = ta_net.node_list[edge.second_node_idx];
    bool is_horizontal = first_node.grid_coord.get_y() == second_node.grid_coord.get_y() && first_node.grid_coord.get_x() != second_node.grid_coord.get_x();
    bool is_vertical = first_node.grid_coord.get_x() == second_node.grid_coord.get_x() && first_node.grid_coord.get_y() != second_node.grid_coord.get_y();
    if (!is_horizontal && !is_vertical) {
      continue;
    }
    int32_t begin_scale = is_horizontal ? std::min(first_node.real_coord.get_x(), second_node.real_coord.get_x())
                                        : std::min(first_node.real_coord.get_y(), second_node.real_coord.get_y());
    int32_t end_scale = is_horizontal ? std::max(first_node.real_coord.get_x(), second_node.real_coord.get_x())
                                      : std::max(first_node.real_coord.get_y(), second_node.real_coord.get_y());
    addTrackUsage(edge.layer_idx, edge.track_coord, begin_scale, end_scale, ta_net.net_idx);
  }
  for (const TAEdge& edge : ta_net.edge_list) {
    if (!edge.is_via) {
      continue;
    }
    reserveViaFootprint(ta_net, edge);
  }
}

void TARouter::assignWireTrack(TANet& ta_net, TAEdge& edge)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  if (edge.layer_idx < 0 || edge.layer_idx >= static_cast<int32_t>(routing_layer_list.size())) {
    return;
  }
  RoutingLayer& routing_layer = routing_layer_list[edge.layer_idx];
  TANode& first_node = ta_net.node_list[edge.first_node_idx];
  TANode& second_node = ta_net.node_list[edge.second_node_idx];

  bool is_horizontal = first_node.grid_coord.get_y() == second_node.grid_coord.get_y();
  bool is_vertical = first_node.grid_coord.get_x() == second_node.grid_coord.get_x();
  if (is_horizontal == is_vertical) {
    return;
  }

  // A pin edge should aim at the AP track; a normal edge stays near the guide center.
  std::vector<int32_t> candidate_track_list;
  int32_t desired_coord = 0;
  if (is_horizontal) {
    desired_coord = edge.guide_rect.getMidPoint().get_y();
    candidate_track_list = RTUTIL.getScaleList(edge.guide_rect.get_ll_y(), edge.guide_rect.get_ur_y(), routing_layer.getYTrackGridList());
  } else {
    desired_coord = edge.guide_rect.getMidPoint().get_x();
    candidate_track_list = RTUTIL.getScaleList(edge.guide_rect.get_ll_x(), edge.guide_rect.get_ur_x(), routing_layer.getXTrackGridList());
  }
  int32_t pin_track_target = getPinTrackTarget(ta_net, edge, is_horizontal);
  if (pin_track_target != -1) {
    desired_coord = pin_track_target;
  }
  if (candidate_track_list.empty()) {
    candidate_track_list.push_back(desired_coord);
  }

  int32_t first_scale = is_horizontal ? first_node.real_coord.get_x() : first_node.real_coord.get_y();
  int32_t second_scale = is_horizontal ? second_node.real_coord.get_x() : second_node.real_coord.get_y();
  if (first_node.is_pin) {
    first_scale = getPinTerminalScale(ta_net, edge.first_node_idx, edge.second_node_idx, is_horizontal);
  }
  if (second_node.is_pin) {
    second_scale = getPinTerminalScale(ta_net, edge.second_node_idx, edge.first_node_idx, is_horizontal);
  }
  int32_t begin_scale = std::min(first_scale, second_scale);
  int32_t end_scale = std::max(first_scale, second_scale);
  edge.candidate_track_list = candidate_track_list;
  edge.begin_scale = begin_scale;
  edge.end_scale = end_scale;

  std::vector<std::pair<int32_t, PlanarRect>> fixed_rect_list;
  {
    EXTLayerRect query_rect;
    query_rect.set_real_rect(edge.guide_rect);
    query_rect.set_layer_idx(edge.layer_idx);
    std::map<int32_t, std::set<EXTLayerRect*>> fixed_rect_map = RTDM.getNetFixedRectMap(true, query_rect);
    for (auto& [net_idx, rect_set] : fixed_rect_map) {
      if (net_idx == ta_net.net_idx) {
        continue;
      }
      for (EXTLayerRect* fixed_rect : rect_set) {
        fixed_rect_list.emplace_back(net_idx, fixed_rect->get_real_rect());
      }
    }
  }

  int32_t half_width = routing_layer.get_min_width() / 2;

  int32_t best_track_coord = candidate_track_list.front();
  int32_t best_valid_track_coord = -1;
  double best_cost = std::numeric_limits<double>::max();
  double best_valid_cost = std::numeric_limits<double>::max();
  for (int32_t track_coord : candidate_track_list) {
    double overlap = getTrackOverlap(edge.layer_idx, track_coord, begin_scale, end_scale, ta_net.net_idx);
    double fixed_cost = 0.0;
    double obstacle_area = 0.0;
    PlanarRect check_rect;
    if (is_horizontal) {
      check_rect.set_ll(edge.guide_rect.get_ll_x(), track_coord - half_width);
      check_rect.set_ur(edge.guide_rect.get_ur_x(), track_coord + half_width);
    } else {
      check_rect.set_ll(track_coord - half_width, edge.guide_rect.get_ll_y());
      check_rect.set_ur(track_coord + half_width, edge.guide_rect.get_ur_y());
    }
    for (const auto& [fixed_net_idx, fixed_rect] : fixed_rect_list) {
      if (!RTUTIL.isOpenOverlap(check_rect, fixed_rect)) {
        continue;
      }
      double overlap_area = RTUTIL.getOverlap(check_rect, fixed_rect).getArea();
      if (fixed_net_idx < 0) {
        // net = -1 is a hard obstacle, not a soft foreign-net shape.
        obstacle_area += overlap_area;
      } else {
        fixed_cost += 1000.0 + overlap_area / 100.0;
      }
    }
    double cost = std::abs(track_coord - desired_coord) + overlap * 20.0 + fixed_cost + getTrackHistory(edge.layer_idx, track_coord);
    if (obstacle_area <= 0.0) {
      if (cost < best_valid_cost) {
        best_valid_cost = cost;
        best_valid_track_coord = track_coord;
      }
    }
    // If every track is blocked, keep the least-bad one instead of dropping the net.
    cost += obstacle_area * 1.0e9;
    if (cost < best_cost) {
      best_cost = cost;
      best_track_coord = track_coord;
    }
  }
  if (best_valid_track_coord != -1) {
    best_track_coord = best_valid_track_coord;
  } else {
    _no_free_track_count++;
  }
  edge.track_coord = best_track_coord;
}

void TARouter::reserveViaFootprint(TANet& ta_net, const TAEdge& edge)
{
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  if (edge.layer_idx < 0 || edge.layer_idx >= static_cast<int32_t>(layer_via_master_list.size())) {
    return;
  }
  if (layer_via_master_list[edge.layer_idx].empty()) {
    return;
  }
  ViaMaster& via_master = layer_via_master_list[edge.layer_idx].front();
  LayerCoord point = ta_net.node_list[edge.first_node_idx].real_coord;

  std::vector<std::pair<int32_t, PlanarRect>> layer_enclosure_list;
  PlanarRect below_rect = RTUTIL.getOffsetRect(via_master.get_below_enclosure(), point.get_planar_coord());
  PlanarRect above_rect = RTUTIL.getOffsetRect(via_master.get_above_enclosure(), point.get_planar_coord());
  layer_enclosure_list.emplace_back(via_master.get_below_enclosure().get_layer_idx(), below_rect);
  layer_enclosure_list.emplace_back(via_master.get_above_enclosure().get_layer_idx(), above_rect);

  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  for (auto& [layer_idx, enclosure_rect] : layer_enclosure_list) {
    if (layer_idx < 0 || layer_idx >= static_cast<int32_t>(routing_layer_list.size())) {
      continue;
    }
    RoutingLayer& routing_layer = routing_layer_list[layer_idx];
    if (routing_layer.isPreferH()) {
      std::vector<int32_t> track_list = RTUTIL.getScaleList(enclosure_rect.get_ll_y(), enclosure_rect.get_ur_y(), routing_layer.getYTrackGridList());
      for (int32_t track_coord : track_list) {
        addTrackUsage(layer_idx, track_coord, enclosure_rect.get_ll_x(), enclosure_rect.get_ur_x(), ta_net.net_idx);
      }
    } else {
      std::vector<int32_t> track_list = RTUTIL.getScaleList(enclosure_rect.get_ll_x(), enclosure_rect.get_ur_x(), routing_layer.getXTrackGridList());
      for (int32_t track_coord : track_list) {
        addTrackUsage(layer_idx, track_coord, enclosure_rect.get_ll_y(), enclosure_rect.get_ur_y(), ta_net.net_idx);
      }
    }
  }
}

void TARouter::rerouteOverflowNets()
{
  constexpr int32_t kMaxIteration = 4;
  for (int32_t iteration = 0; iteration < kMaxIteration; iteration++) {
    std::set<int32_t> overflow_net_set = collectOverflowNetSet();
    if (overflow_net_set.empty()) {
      RTLOG.info(Loc::current(), "No track overflow after TA iteration ", iteration);
      return;
    }
    RTLOG.info(Loc::current(), "TA reroute iteration ", iteration, ": ", overflow_net_set.size(), " overflow nets");
    updateTrackHistory();
    for (TANet& ta_net : _ta_net_list) {
      if (!overflow_net_set.contains(ta_net.net_idx)) {
        continue;
      }
      ripupNet(ta_net);
      assignTracks(ta_net);
    }
  }
}

std::set<int32_t> TARouter::collectOverflowNetSet() const
{
  std::set<int32_t> overflow_net_set;
  for (const auto& [layer_idx, track_map] : _track_usage) {
    for (const auto& [track_coord, interval_list] : track_map) {
      if (interval_list.size() < 2) {
        continue;
      }
      std::vector<TrackInterval> sorted_list = interval_list;
      std::sort(sorted_list.begin(), sorted_list.end(), [](const TrackInterval& a, const TrackInterval& b) { return a.begin < b.begin; });
      for (size_t i = 0; i < sorted_list.size(); i++) {
        for (size_t j = i + 1; j < sorted_list.size() && sorted_list[j].begin < sorted_list[i].end; j++) {
          if (sorted_list[i].net_idx == sorted_list[j].net_idx) {
            continue;
          }
          overflow_net_set.insert(sorted_list[i].net_idx);
          overflow_net_set.insert(sorted_list[j].net_idx);
        }
      }
    }
  }
  return overflow_net_set;
}

void TARouter::updateTrackHistory()
{
  for (const auto& [layer_idx, track_map] : _track_usage) {
    for (const auto& [track_coord, interval_list] : track_map) {
      if (interval_list.size() < 2) {
        continue;
      }
      std::vector<TrackInterval> sorted_list = interval_list;
      std::sort(sorted_list.begin(), sorted_list.end(), [](const TrackInterval& a, const TrackInterval& b) { return a.begin < b.begin; });
      bool has_overflow = false;
      for (size_t i = 0; i < sorted_list.size() && !has_overflow; i++) {
        for (size_t j = i + 1; j < sorted_list.size() && sorted_list[j].begin < sorted_list[i].end; j++) {
          if (sorted_list[i].net_idx != sorted_list[j].net_idx) {
            has_overflow = true;
            break;
          }
        }
      }
      if (has_overflow) {
        _track_history[layer_idx][track_coord] += 50.0;
      }
    }
  }
}

double TARouter::getTrackHistory(int32_t layer_idx, int32_t track_coord) const
{
  auto layer_iter = _track_history.find(layer_idx);
  if (layer_iter == _track_history.end()) {
    return 0.0;
  }
  auto track_iter = layer_iter->second.find(track_coord);
  if (track_iter == layer_iter->second.end()) {
    return 0.0;
  }
  return track_iter->second;
}

void TARouter::ripupNet(const TANet& ta_net)
{
  for (auto layer_iter = _track_usage.begin(); layer_iter != _track_usage.end();) {
    auto& track_map = layer_iter->second;
    for (auto track_iter = track_map.begin(); track_iter != track_map.end();) {
      std::vector<TrackInterval>& interval_list = track_iter->second;
      interval_list.erase(std::remove_if(interval_list.begin(), interval_list.end(),
                                         [&ta_net](const TrackInterval& interval) { return interval.net_idx == ta_net.net_idx; }),
                          interval_list.end());
      if (interval_list.empty()) {
        track_iter = track_map.erase(track_iter);
      } else {
        ++track_iter;
      }
    }
    if (track_map.empty()) {
      layer_iter = _track_usage.erase(layer_iter);
    } else {
      ++layer_iter;
    }
  }
}

bool TARouter::isHorizontalEdge(const TANet& ta_net, const TAEdge& edge) const
{
  const TANode& first_node = ta_net.node_list[edge.first_node_idx];
  const TANode& second_node = ta_net.node_list[edge.second_node_idx];
  return first_node.grid_coord.get_y() == second_node.grid_coord.get_y() && first_node.grid_coord.get_x() != second_node.grid_coord.get_x();
}

double TARouter::getTrackOverlap(int32_t layer_idx, int32_t track_coord, int32_t begin, int32_t end, int32_t net_idx) const
{
  auto layer_iter = _track_usage.find(layer_idx);
  if (layer_iter == _track_usage.end()) {
    return 0.0;
  }
  auto track_iter = layer_iter->second.find(track_coord);
  if (track_iter == layer_iter->second.end()) {
    return 0.0;
  }
  double overlap = 0.0;
  for (const TrackInterval& interval : track_iter->second) {
    if (interval.net_idx == net_idx) {
      continue;
    }
    int32_t overlap_begin = std::max(begin, interval.begin);
    int32_t overlap_end = std::min(end, interval.end);
    if (overlap_begin < overlap_end) {
      overlap += static_cast<double>(overlap_end - overlap_begin);
    }
  }
  return overlap;
}

void TARouter::addTrackUsage(int32_t layer_idx, int32_t track_coord, int32_t begin, int32_t end, int32_t net_idx)
{
  if (begin > end) {
    std::swap(begin, end);
  }
  TrackInterval interval;
  interval.begin = begin;
  interval.end = end;
  interval.net_idx = net_idx;
  _track_usage[layer_idx][track_coord].push_back(interval);
}

void TARouter::outputNet(TANet& ta_net)
{
  std::vector<Segment<LayerCoord>> segment_list;
  for (const TAEdge& edge : ta_net.edge_list) {
    if (edge.is_via) {
      LayerCoord point = ta_net.node_list[edge.first_node_idx].real_coord;
      addViaSegment(segment_list, point, edge.layer_idx);
      continue;
    }

    TANode& first_node = ta_net.node_list[edge.first_node_idx];
    TANode& second_node = ta_net.node_list[edge.second_node_idx];
    LayerCoord first_coord = first_node.real_coord;
    LayerCoord second_coord = second_node.real_coord;
    bool is_horizontal = first_node.grid_coord.get_y() == second_node.grid_coord.get_y() && first_node.grid_coord.get_x() != second_node.grid_coord.get_x();
    bool is_vertical = first_node.grid_coord.get_x() == second_node.grid_coord.get_x() && first_node.grid_coord.get_y() != second_node.grid_coord.get_y();
    if (!is_horizontal && !is_vertical) {
      continue;
    }
    int32_t layer_idx = edge.layer_idx;

    LayerCoord main_first;
    LayerCoord main_second;
    if (is_horizontal) {
      main_first = LayerCoord(first_coord.get_x(), edge.track_coord, layer_idx);
      main_second = LayerCoord(second_coord.get_x(), edge.track_coord, layer_idx);
    } else {
      main_first = LayerCoord(edge.track_coord, first_coord.get_y(), layer_idx);
      main_second = LayerCoord(edge.track_coord, second_coord.get_y(), layer_idx);
    }
    addWireSegment(segment_list, main_first, main_second);
  }

  if (segment_list.empty()) {
    buildFallbackResult(ta_net, segment_list);
    _fallback_net_set.insert(ta_net.net_idx);
  }
  if (!segment_list.empty()) {
    RTDM.getDatabase().get_net_detailed_result_map()[ta_net.net_idx] = std::move(segment_list);
  }
}

void TARouter::buildFallbackResult(TANet& ta_net, std::vector<Segment<LayerCoord>>& segment_list)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<Segment<LayerCoord>>& global_result_list = RTDM.getDatabase().get_net_global_result_map()[ta_net.net_idx];
  for (Segment<LayerCoord>& segment : global_result_list) {
    LayerCoord& first_coord = segment.get_first();
    LayerCoord& second_coord = segment.get_second();
    PlanarCoord first_real = RTUTIL.getRealRectByGCell(first_coord, gcell_axis).getMidPoint();
    PlanarCoord second_real = RTUTIL.getRealRectByGCell(second_coord, gcell_axis).getMidPoint();
    if (first_coord.get_layer_idx() == second_coord.get_layer_idx()) {
      addWireSegment(segment_list, LayerCoord(first_real, first_coord.get_layer_idx()), LayerCoord(second_real, first_coord.get_layer_idx()));
      continue;
    }
    int32_t bottom_layer_idx = std::min(first_coord.get_layer_idx(), second_coord.get_layer_idx());
    int32_t top_layer_idx = std::max(first_coord.get_layer_idx(), second_coord.get_layer_idx());
    for (int32_t layer_idx = bottom_layer_idx; layer_idx < top_layer_idx; layer_idx++) {
      addViaSegment(segment_list, LayerCoord(first_real, layer_idx), layer_idx);
    }
  }
}

void TARouter::addWireSegment(std::vector<Segment<LayerCoord>>& segment_list, const LayerCoord& first_coord, const LayerCoord& second_coord)
{
  if (first_coord == second_coord) {
    return;
  }
  if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
    return;
  }
  if (first_coord.get_x() != second_coord.get_x() && first_coord.get_y() != second_coord.get_y()) {
    return;
  }
  segment_list.emplace_back(first_coord, second_coord);
}

void TARouter::addViaSegment(std::vector<Segment<LayerCoord>>& segment_list, const LayerCoord& coord, int32_t below_layer_idx)
{
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  if (below_layer_idx < 0 || below_layer_idx + 1 >= static_cast<int32_t>(layer_via_master_list.size())) {
    return;
  }
  if (layer_via_master_list[below_layer_idx].empty()) {
    return;
  }
  ViaMasterIdx via_master_idx = layer_via_master_list[below_layer_idx].front().get_via_master_idx();
  LayerCoord below_coord(coord.get_x(), coord.get_y(), below_layer_idx);
  LayerCoord above_coord(coord.get_x(), coord.get_y(), below_layer_idx + 1);
  segment_list.emplace_back(below_coord, above_coord, via_master_idx);
}

}  // namespace irt
