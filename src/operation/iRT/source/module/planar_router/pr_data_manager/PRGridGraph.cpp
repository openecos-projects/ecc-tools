// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#include "PRGridGraph.hpp"

#include "PRNet.hpp"
#include "Utility.hpp"

namespace irt {

namespace {

constexpr int32_t kNeighborOffsetX[4] = {1, -1, 0, 0};
constexpr int32_t kNeighborOffsetY[4] = {0, 0, 1, -1};

}  // namespace

PRGridGraph::NetRouteTrial::NetRouteTrial(PRGridGraph& graph, PRNet& pr_net)
    : _graph(&graph), _pr_net(&pr_net), _old_edge_set(pr_net.get_routing_edge_set()), _net_idx(pr_net.get_net_idx())
{
  // Keep the old route as a snapshot and release its demand for this trial.
  for (PREdgeId edge_id : _old_edge_set) {
    _graph->update_edge(edge_id, _net_idx, -1);
  }
}

void PRGridGraph::NetRouteTrial::append(std::span<const Segment<PlanarCoord>> routing_segment_list)
{
  if (_is_finished) {
    RTLOG.error(Loc::current(), "The planar route trial is already finished!");
  }
  PREdgeSet edge_set = _graph->get_edge_set(routing_segment_list);
  _trial_edge_set.insert(edge_set.begin(), edge_set.end());
}

void PRGridGraph::NetRouteTrial::commit(std::vector<Segment<PlanarCoord>>&& routing_segment_list)
{
  if (_is_finished) {
    RTLOG.error(Loc::current(), "The planar route trial is already finished!");
  }
  _graph->commit_route(*this, std::move(routing_segment_list));
  _is_finished = true;
}

void PRGridGraph::NetRouteTrial::rollback()
{
  if (_is_finished) {
    RTLOG.error(Loc::current(), "The planar route trial is already finished!");
  }
  for (PREdgeId edge_id : _old_edge_set) {
    _graph->update_edge(edge_id, _net_idx, 1);
  }
  _is_finished = true;
}

void PRGridGraph::build(Database& database)
{
  GridMap<PlanarRect>& gcell_map = database.get_gcell_map();
  _x_size = gcell_map.get_x_size();
  _y_size = gcell_map.get_y_size();
  _node_list.clear();
  _node_list.resize(static_cast<size_t>(_x_size) * _y_size);
  for (int32_t x = 0; x < _x_size; x++) {
    for (int32_t y = 0; y < _y_size; y++) {
      PRGridNode& node = _node_list[static_cast<size_t>(x) * _y_size + y];
      node.rect = gcell_map[x][y];
      node.is_blocked = false;
    }
  }

  ScaleAxis& gcell_axis = database.get_gcell_axis();
  _macro_grid_rect_list.clear();
  for (Macro& macro : database.get_macro_list()) {
    _macro_grid_rect_list.push_back(RTUTIL.getClosedGCellGridRect(macro.get_body_rect(), gcell_axis));
  }
  for (const PlanarRect& macro_rect : _macro_grid_rect_list) {
    for (int32_t x = std::max(0, macro_rect.get_ll_x()); x <= std::min(_x_size - 1, macro_rect.get_ur_x()); x++) {
      for (int32_t y = std::max(0, macro_rect.get_ll_y()); y <= std::min(_y_size - 1, macro_rect.get_ur_y()); y++) {
        _node_list[static_cast<size_t>(x) * _y_size + y].is_blocked = true;
      }
    }
  }

  // 多源 BFS：计算每个 gcell 到最近 blocked cell 的距离，只展开到 halo 半径
  std::vector<int32_t> distance_queue;
  for (size_t node_idx = 0; node_idx < _node_list.size(); node_idx++) {
    if (_node_list[node_idx].is_blocked) {
      _node_list[node_idx].blocked_distance = 0;
      distance_queue.push_back(static_cast<int32_t>(node_idx));
    }
  }
  for (size_t head = 0; head < distance_queue.size(); head++) {
    int32_t node_idx = distance_queue[head];
    int32_t next_distance = _node_list[node_idx].blocked_distance + 1;
    if (next_distance > kMacroHaloRadius) {
      continue;
    }
    int32_t x = node_idx / _y_size;
    int32_t y = node_idx % _y_size;
    for (int32_t neighbor = 0; neighbor < 4; neighbor++) {
      int32_t neighbor_x = x + kNeighborOffsetX[neighbor];
      int32_t neighbor_y = y + kNeighborOffsetY[neighbor];
      if (neighbor_x < 0 || _x_size <= neighbor_x || neighbor_y < 0 || _y_size <= neighbor_y) {
        continue;
      }
      int32_t neighbor_idx = neighbor_x * _y_size + neighbor_y;
      if (_node_list[neighbor_idx].blocked_distance <= next_distance) {
        continue;
      }
      _node_list[neighbor_idx].blocked_distance = next_distance;
      distance_queue.push_back(neighbor_idx);
    }
  }

  _horizontal_edge_list.clear();
  _vertical_edge_list.clear();
  _horizontal_edge_list.resize(static_cast<size_t>(get_horizontal_x_size()) * _y_size);
  _vertical_edge_list.resize(static_cast<size_t>(_x_size) * get_vertical_y_size());
  std::vector<GridMap<RoutingEdge>>& horizontal_layer_map_list = database.get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& vertical_layer_map_list = database.get_routing_v_edge_map();
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(horizontal_layer_map_list.size()); layer_idx++) {
    GridMap<RoutingEdge>& horizontal_layer_map = horizontal_layer_map_list[layer_idx];
    GridMap<RoutingEdge>& vertical_layer_map = vertical_layer_map_list[layer_idx];
    for (int32_t x = 0; x < horizontal_layer_map.get_x_size(); x++) {
      for (int32_t y = 0; y < horizontal_layer_map.get_y_size(); y++) {
        PRGridEdge& edge = _horizontal_edge_list[get_horizontal_edge_index(x, y)];
        RoutingEdge& layer_edge = horizontal_layer_map[x][y];
        edge.supply += layer_edge.get_supply();
        edge.congestion_cost += layer_edge.get_congestion_cost();
        for (int32_t ignore_net_idx : layer_edge.get_ignore_net_set()) {
          if (!edge.isIgnoreNet(ignore_net_idx)) {
            edge.ignore_net_idx_list.push_back(ignore_net_idx);
          }
        }
      }
    }
    for (int32_t x = 0; x < vertical_layer_map.get_x_size(); x++) {
      for (int32_t y = 0; y < vertical_layer_map.get_y_size(); y++) {
        PRGridEdge& edge = _vertical_edge_list[get_vertical_edge_index(x, y)];
        RoutingEdge& layer_edge = vertical_layer_map[x][y];
        edge.supply += layer_edge.get_supply();
        edge.congestion_cost += layer_edge.get_congestion_cost();
        for (int32_t ignore_net_idx : layer_edge.get_ignore_net_set()) {
          if (!edge.isIgnoreNet(ignore_net_idx)) {
            edge.ignore_net_idx_list.push_back(ignore_net_idx);
          }
        }
      }
    }
  }

  // macro 内圈进一步压低 planar supply，使这些边更早进入饱和/overflow
  for (int32_t x = 0; x < get_horizontal_x_size(); x++) {
    for (int32_t y = 0; y < _y_size; y++) {
      PRGridEdge& edge = _horizontal_edge_list[get_horizontal_edge_index(x, y)];
      if (edge.supply <= 0) {
        continue;
      }
      int32_t halo_distance = std::min(get_blocked_distance({x, y}), get_blocked_distance({x + 1, y}));
      if (halo_distance <= kMacroSupplyReduceRadius) {
        edge.supply = std::max(1, edge.supply - kMacroSupplyReduceNum);
      }
    }
  }
  for (int32_t x = 0; x < _x_size; x++) {
    for (int32_t y = 0; y < get_vertical_y_size(); y++) {
      PRGridEdge& edge = _vertical_edge_list[get_vertical_edge_index(x, y)];
      if (edge.supply <= 0) {
        continue;
      }
      int32_t halo_distance = std::min(get_blocked_distance({x, y}), get_blocked_distance({x, y + 1}));
      if (halo_distance <= kMacroSupplyReduceRadius) {
        edge.supply = std::max(1, edge.supply - kMacroSupplyReduceNum);
      }
    }
  }
}

const PRGridNode& PRGridGraph::get_node(const PlanarCoord& coord) const
{
  if (coord.get_x() < 0 || _x_size <= coord.get_x() || coord.get_y() < 0 || _y_size <= coord.get_y()) {
    RTLOG.error(Loc::current(), "The planar graph node is outside the graph!");
  }
  return _node_list[static_cast<size_t>(coord.get_x()) * _y_size + coord.get_y()];
}

bool PRGridGraph::isCellLegal(const PlanarCoord& coord) const
{
  if (coord.get_x() < 0 || _x_size <= coord.get_x() || coord.get_y() < 0 || _y_size <= coord.get_y()) {
    return false;
  }
  return !get_node(coord).is_blocked;
}

bool PRGridGraph::isCellInMacroHalo(const PlanarCoord& coord) const
{
  if (coord.get_x() < 0 || _x_size <= coord.get_x() || coord.get_y() < 0 || _y_size <= coord.get_y()) {
    return false;
  }
  return get_blocked_distance(coord) <= kMacroHaloRadius;
}

uint32_t PRGridGraph::get_horizontal_edge_index(int32_t x, int32_t y) const
{
  if (x < 0 || get_horizontal_x_size() <= x || y < 0 || _y_size <= y) {
    RTLOG.error(Loc::current(), "The planar horizontal edge is outside the graph!");
  }
  return static_cast<uint32_t>(x * _y_size + y);
}

uint32_t PRGridGraph::get_vertical_edge_index(int32_t x, int32_t y) const
{
  if (x < 0 || _x_size <= x || y < 0 || get_vertical_y_size() <= y) {
    RTLOG.error(Loc::current(), "The planar vertical edge is outside the graph!");
  }
  return static_cast<uint32_t>(x * get_vertical_y_size() + y);
}

PREdgeId PRGridGraph::get_edge_id(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const
{
  int32_t dx = second_coord.get_x() - first_coord.get_x();
  int32_t dy = second_coord.get_y() - first_coord.get_y();
  if (std::abs(dx) + std::abs(dy) != 1) {
    RTLOG.error(Loc::current(), "The planar graph edge must connect adjacent gcells!");
  }
  int32_t x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t y = std::min(first_coord.get_y(), second_coord.get_y());
  if (dx != 0) {
    return {get_horizontal_edge_index(x, y)};
  }
  uint32_t vertical_offset = static_cast<uint32_t>(get_horizontal_x_size() * _y_size);
  return {vertical_offset + get_vertical_edge_index(x, y)};
}

const PRGridEdge& PRGridGraph::get_edge(PREdgeId edge_id) const
{
  uint32_t horizontal_edge_num = static_cast<uint32_t>(get_horizontal_x_size() * _y_size);
  if (edge_id.value < horizontal_edge_num) {
    return _horizontal_edge_list[edge_id.value];
  }
  uint32_t vertical_idx = edge_id.value - horizontal_edge_num;
  if (vertical_idx >= _vertical_edge_list.size()) {
    RTLOG.error(Loc::current(), "The planar graph edge id is invalid!");
  }
  return _vertical_edge_list[vertical_idx];
}

const PRGridEdge& PRGridGraph::get_horizontal_edge(int32_t x, int32_t y) const
{
  return _horizontal_edge_list[get_horizontal_edge_index(x, y)];
}

const PRGridEdge& PRGridGraph::get_vertical_edge(int32_t x, int32_t y) const
{
  return _vertical_edge_list[get_vertical_edge_index(x, y)];
}

PREdgeCost PRGridGraph::get_edge_cost(int32_t supply, int32_t demand) const
{
  constexpr double saturation_start_ratio = 0.8;
  constexpr double hotspot_start_ratio = 0.9;
  PREdgeCost edge_cost;
  if (supply <= 0 || demand > supply) {
    double overflow_ratio = demand - std::max(supply, 0) + 1;
    double overflow_ratio_sqr = overflow_ratio * overflow_ratio;
    edge_cost.is_overflow = true;
    edge_cost.unit_cost = overflow_ratio_sqr * overflow_ratio_sqr * overflow_ratio_sqr;
    return edge_cost;
  }
  double usage_ratio = demand / 1.0 / supply;
  double usage_ratio_sqr = usage_ratio * usage_ratio;
  edge_cost.unit_cost = usage_ratio_sqr * usage_ratio_sqr;
  if (usage_ratio < saturation_start_ratio) {
    return edge_cost;
  }
  double saturation_ratio = (usage_ratio - saturation_start_ratio) / (1.0 - saturation_start_ratio);
  edge_cost.is_saturated = true;
  edge_cost.unit_cost += saturation_ratio * saturation_ratio;
  if (usage_ratio < hotspot_start_ratio) {
    return edge_cost;
  }
  double hotspot_ratio = (usage_ratio - hotspot_start_ratio) / (1.0 - hotspot_start_ratio);
  edge_cost.is_hotspot = true;
  edge_cost.unit_cost += 2.0 * hotspot_ratio * hotspot_ratio;
  return edge_cost;
}

PREdgeCost PRGridGraph::get_edge_cost(PREdgeId edge_id) const
{
  const PRGridEdge& edge = get_edge(edge_id);
  return get_edge_cost(edge.supply, edge.demand + 1);
}

double PRGridGraph::get_edge_cost(PREdgeId edge_id, double overflow_unit) const
{
  const PRGridEdge& edge = get_edge(edge_id);
  if (edge.supply <= 0) {
    return std::numeric_limits<double>::infinity();
  }
  PREdgeCost edge_cost = get_edge_cost(edge_id);
  return 1.0 + edge_cost.getTotalCost(overflow_unit, edge.congestion_cost);
}

double PRGridGraph::get_segment_cost(int32_t net_idx, const PlanarCoord& first_coord, const PlanarCoord& second_coord,
                                     double overflow_unit) const
{
  if (first_coord == second_coord) {
    return 0;
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    return std::numeric_limits<double>::infinity();
  }
  int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
  double segment_cost = 0;
  if (RTUTIL.isHorizontal(first_coord, second_coord)) {
    for (int32_t x = first_x; x < second_x; x++) {
      PREdgeId edge_id = get_edge_id({x, first_y}, {x + 1, first_y});
      segment_cost += get_edge(edge_id).isIgnoreNet(net_idx) ? 1.0 : get_edge_cost(edge_id, overflow_unit);
      segment_cost += get_edge_halo_cost({x, first_y}, {x + 1, first_y});
    }
  } else {
    for (int32_t y = first_y; y < second_y; y++) {
      PREdgeId edge_id = get_edge_id({first_x, y}, {first_x, y + 1});
      segment_cost += get_edge(edge_id).isIgnoreNet(net_idx) ? 1.0 : get_edge_cost(edge_id, overflow_unit);
      segment_cost += get_edge_halo_cost({first_x, y}, {first_x, y + 1});
    }
  }
  return segment_cost;
}

PREdgeSet PRGridGraph::get_edge_set(std::span<const Segment<PlanarCoord>> routing_segment_list) const
{
  PREdgeSet edge_set;
  for (const Segment<PlanarCoord>& segment : routing_segment_list) {
    PlanarCoord first_coord = segment.get_first();
    PlanarCoord second_coord = segment.get_second();
    if (first_coord == second_coord) {
      continue;
    }
    if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
      RTLOG.error(Loc::current(), "The routing segment is oblique!");
    }
    int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
    int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
    if (RTUTIL.isHorizontal(first_coord, second_coord)) {
      for (int32_t x = first_x; x < second_x; x++) {
        edge_set.insert(get_edge_id({x, first_y}, {x + 1, first_y}));
      }
    } else {
      for (int32_t y = first_y; y < second_y; y++) {
        edge_set.insert(get_edge_id({first_x, y}, {first_x, y + 1}));
      }
    }
  }
  return edge_set;
}

void PRGridGraph::update_edge(PREdgeId edge_id, int32_t net_idx, int32_t delta)
{
  uint32_t horizontal_edge_num = static_cast<uint32_t>(get_horizontal_x_size() * _y_size);
  PRGridEdge& edge = edge_id.value < horizontal_edge_num ? _horizontal_edge_list[edge_id.value]
                                                          : _vertical_edge_list[edge_id.value - horizontal_edge_num];
  if (edge.isIgnoreNet(net_idx)) {
    return;
  }
  if (delta < 0 && edge.demand <= 0) {
    RTLOG.error(Loc::current(), "The planar graph edge demand is error!");
  }
  if (delta > 0) {
    edge.demand_net_idx_list.push_back(net_idx);
  } else {
    auto iter = std::find(edge.demand_net_idx_list.begin(), edge.demand_net_idx_list.end(), net_idx);
    if (iter == edge.demand_net_idx_list.end()) {
      RTLOG.error(Loc::current(), "The planar graph edge demand net is error!");
    }
    edge.demand_net_idx_list.erase(iter);
  }
  edge.demand += delta;
}

void PRGridGraph::commit_route(NetRouteTrial& trial, std::vector<Segment<PlanarCoord>>&& routing_segment_list)
{
  PREdgeSet final_edge_set = get_edge_set(routing_segment_list);
  // The old route was released when the trial started. Commit only the final route.
  for (PREdgeId edge_id : final_edge_set) {
    update_edge(edge_id, trial.get_net_idx(), 1);
  }
  trial._pr_net->set_routing_segment_list(std::move(routing_segment_list));
  trial._pr_net->set_routing_edge_set(std::move(final_edge_set));
}

void PRGridGraph::update_congestion(double overflow_unit)
{
  update_congestion(_horizontal_edge_list, get_horizontal_x_size(), _y_size, overflow_unit);
  update_congestion(_vertical_edge_list, _x_size, get_vertical_y_size(), overflow_unit);
}

void PRGridGraph::update_congestion(std::vector<PRGridEdge>& edge_list, int32_t x_size, int32_t y_size, double overflow_unit)
{
  constexpr int32_t congestion_radius = 1;
  constexpr double congestion_decay = 0.5;
#pragma omp parallel for
  for (int64_t edge_idx = 0; edge_idx < static_cast<int64_t>(edge_list.size()); edge_idx++) {
    int32_t x = static_cast<int32_t>(edge_idx / y_size);
    int32_t y = static_cast<int32_t>(edge_idx % y_size);
    double total_usage_ratio = 0;
    int32_t neighbor_num = 0;
    for (int32_t neighbor_x = std::max(0, x - congestion_radius); neighbor_x <= std::min(x_size - 1, x + congestion_radius); neighbor_x++) {
      for (int32_t neighbor_y = std::max(0, y - congestion_radius); neighbor_y <= std::min(y_size - 1, y + congestion_radius); neighbor_y++) {
        const PRGridEdge& neighbor_edge = edge_list[static_cast<size_t>(neighbor_x) * y_size + neighbor_y];
        if (neighbor_edge.supply == 0) {
          continue;
        }
        total_usage_ratio += neighbor_edge.demand / 1.0 / neighbor_edge.supply;
        neighbor_num++;
      }
    }
    PRGridEdge& edge = edge_list[edge_idx];
    double usage_ratio = edge.supply == 0 ? 0 : edge.demand / 1.0 / edge.supply;
    usage_ratio = std::max(0.0, usage_ratio - 0.8);
    double average_usage_ratio = neighbor_num == 0 ? 0 : total_usage_ratio / neighbor_num;
    double congestion_ratio = average_usage_ratio + usage_ratio;
    double new_congestion_cost = overflow_unit * congestion_ratio * congestion_ratio;
    edge.congestion_cost = edge.congestion_cost * congestion_decay + new_congestion_cost;
  }
}

}  // namespace irt
