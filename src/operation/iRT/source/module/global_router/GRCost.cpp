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
#include "GRCost.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Logger.hpp"
#include "Net.hpp"
#include "Pin.hpp"

namespace irt {

void GRMazeCostContext::init(const GridMap<PlanarRect>& gcell_map, int32_t micron_dbu, const std::vector<GRNet>& net_list,
                             const std::map<int32_t, double>& net_steiner_length_map)
{
  int32_t x_size = gcell_map.get_x_size();
  int32_t y_size = gcell_map.get_y_size();
  if (x_size <= 0 || y_size <= 0 || micron_dbu <= 0) {
    RTLOG.error(Loc::current(), "Invalid GCell geometry for maze length cost!");
  }
  x_coord_um_list_.resize(x_size);
  y_coord_um_list_.resize(y_size);
  double total_span_um = 0.0;
  for (int32_t x = 0; x < x_size; x++) {
    const PlanarRect& rect = gcell_map[x][0];
    x_coord_um_list_[x] = static_cast<double>(rect.getMidPoint().get_x()) / micron_dbu;
    total_span_um += static_cast<double>(rect.getXSpan()) / micron_dbu;
  }
  for (int32_t y = 0; y < y_size; y++) {
    const PlanarRect& rect = gcell_map[0][y];
    y_coord_um_list_[y] = static_cast<double>(rect.getMidPoint().get_y()) / micron_dbu;
    total_span_um += static_cast<double>(rect.getYSpan()) / micron_dbu;
  }
  unit_length_um_ = total_span_um / (static_cast<double>(x_size) + y_size);
  if (!std::isfinite(unit_length_um_) || unit_length_um_ <= 0.0) {
    RTLOG.error(Loc::current(), "Invalid GCell unit length for maze length cost!");
  }

  reference_length_um_map_.clear();
  for (const GRNet& net : net_list) {
    int32_t net_idx = net.get_origin_net()->get_net_idx();
    double steiner_length_um = 0.0;
    auto iter = net_steiner_length_map.find(net_idx);
    if (iter != net_steiner_length_map.end()) {
      steiner_length_um = iter->second;
    } else {
      const std::vector<GRPin>& pin_list = net.get_pin_list();
      for (const GRPin& pin : pin_list) {
        if (pin.pin->get_access_point().get_real_coord() != pin_list.front().pin->get_access_point().get_real_coord()) {
          RTLOG.error(Loc::current(), "Missing Steiner length for maze net ", net.get_origin_net()->get_net_name(), "!");
        }
      }
    }
    if (!std::isfinite(steiner_length_um) || steiner_length_um < 0.0) {
      RTLOG.error(Loc::current(), "Invalid Steiner length for maze net ", net.get_origin_net()->get_net_name(), "!");
    }
    reference_length_um_map_.emplace(net_idx, std::max(steiner_length_um, unit_length_um_));
  }
}

double GRMazeCostContext::getWireLength(const PlanarCoord& first, const PlanarCoord& second) const
{
  return std::abs(x_coord_um_list_[first.get_x()] - x_coord_um_list_[second.get_x()])
         + std::abs(y_coord_um_list_[first.get_y()] - y_coord_um_list_[second.get_y()]);
}

double GRMazeCostContext::getTreeLength(const GRTree& tree) const
{
  double length_um = 0.0;
  for (const GRWireSegment& wire : tree.wire_list) {
    if (wire.first.get_layer_idx() == wire.second.get_layer_idx()) {
      length_um += getWireLength(wire.first, wire.second);
    }
  }
  return length_um;
}

double GRMazeCostContext::getLengthWeight(const GRParams& params) const
{
  if (!std::isfinite(params.maze_wire_length_weight) || params.maze_wire_length_weight < 0.0 || !std::isfinite(params.maze_detour_ratio_weight)
      || params.maze_detour_ratio_weight < 0.0 || !std::isfinite(params.maze_detour_ratio_free) || params.maze_detour_ratio_free < 1.0
      || !std::isfinite(params.maze_detour_ratio_reweight_start) || params.maze_detour_ratio_reweight_start < params.maze_detour_ratio_free
      || params.maze_detour_ratio_power < 2 || !std::isfinite(params.initial_cost_weight) || params.initial_cost_weight <= 0.0
      || !std::isfinite(params.cost_weight) || params.cost_weight < 0.0) {
    RTLOG.error(Loc::current(), "Invalid parameters for maze length cost!");
  }
  double scale = params.cost_weight / params.initial_cost_weight;
  return scale * params.maze_wire_length_weight / unit_length_um_;
}

double GRMazeCostContext::getReweightedLengthWeight(const GRNet& net, const GRParams& params, const GRTree& tree) const
{
  double length_weight = getLengthWeight(params);
  double reference_length_um = reference_length_um_map_.at(net.get_origin_net()->get_net_idx());
  double ratio = getTreeLength(tree) / reference_length_um;
  if (params.maze_detour_ratio_weight == 0.0 || ratio <= params.maze_detour_ratio_reweight_start) {
    return length_weight;
  }
  double scale = params.cost_weight / params.initial_cost_weight;
  double excess = ratio - params.maze_detour_ratio_free;
  double ratio_slope
      = scale * params.maze_detour_ratio_weight * params.maze_detour_ratio_power * std::pow(excess, params.maze_detour_ratio_power - 1) / reference_length_um;
  return addCost(length_weight, ratio_slope);
}

double GRMazeCostContext::getTreeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRTree& tree) const
{
  double length_weight = getLengthWeight(params);
  double length_um = getTreeLength(tree);
  double length_cost = length_weight * length_um;
  if (params.maze_detour_ratio_weight > 0.0) {
    double reference_length_um = reference_length_um_map_.at(net.get_origin_net()->get_net_idx());
    double excess = std::max(0.0, length_um / reference_length_um - params.maze_detour_ratio_free);
    double scale = params.cost_weight / params.initial_cost_weight;
    double ratio_cost = scale * params.maze_detour_ratio_weight * std::pow(excess, params.maze_detour_ratio_power);
    length_cost = addCost(length_cost, ratio_cost);
  }
  return addCost(treeCost(grid, net, params, tree), length_cost);
}

double edgeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRWireEdgeId& edge_id, double amount)
{
  if (edge_id.layer_idx < grid.getBottomLayerIdx() || edge_id.layer_idx > grid.getTopLayerIdx()) {
    return GRParams::kInf;
  }
  if (!grid.hasWireEdge(edge_id)) {
    return GRParams::kInf;
  }
  if (net.isExempt(edge_id)) {
    return amount * params.unit_wire_cost;
  }
  const GRWireEdge& edge = grid.getWireEdge(edge_id);
  double usable_supply = edge.getUsableSupply();
  if (usable_supply <= GRParams::kEpsilon) {
    return GRParams::kInf;
  }
  return amount * (params.unit_wire_cost + params.cost_weight * congestionCost(params, usable_supply, edge.getDemand(), amount));
}

double congestionCost(const GRParams& params, double supply, double demand, double amount)
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
  double hotspot_start_ratio = params.congestion_hotspot_ratio;
  if (usage_ratio < hotspot_start_ratio) {
    return unit_cost;
  }
  double hotspot_ratio = (usage_ratio - hotspot_start_ratio) / (1.0 - hotspot_start_ratio);
  unit_cost += 2.0 * hotspot_ratio * hotspot_ratio;
  return unit_cost;
}

double directionCost(const GRParams& params)
{
  return params.via_base_cost * (params.cost_weight / params.initial_cost_weight);
}

double addCost(double first, double second)
{
  if (first >= GRParams::kInf || second >= GRParams::kInf) {
    return GRParams::kInf;
  }
  double sum = first + second;
  return sum >= GRParams::kInf ? GRParams::kInf : sum;
}

double viaCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const PlanarCoord& coord, int32_t below_layer_idx)
{
  if (below_layer_idx < grid.getBottomLayerIdx() || below_layer_idx >= grid.getTopLayerIdx()) {
    return GRParams::kInf;
  }
  double total_cost = params.via_base_cost;
  for (int32_t side_layer_idx : {below_layer_idx, below_layer_idx + 1}) {
    if (net.isAPViaSide(coord, side_layer_idx)) {
      continue;
    }
    for (GRViaEdgeIter iter(grid, coord, side_layer_idx); iter.isValid(); iter.next()) {
      const GRWireEdgeId& edge_id = iter.getEdgeId();
      if (!grid.hasUsableSupply(edge_id)) {
        continue;
      }
      double one_edge_cost = edgeCost(grid, net, params, edge_id, params.via_demand);
      if (one_edge_cost >= GRParams::kInf) {
        return GRParams::kInf;
      }
      total_cost += one_edge_cost;
    }
  }
  return total_cost;
}

double segmentCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, int32_t layer_idx, const PlanarCoord& first_coord,
                   const PlanarCoord& second_coord)
{
  if (first_coord == second_coord) {
    return 0.0;
  }
  if (layer_idx < grid.getBottomLayerIdx() || layer_idx > grid.getTopLayerIdx()) {
    return GRParams::kInf;
  }
  bool is_horizontal_segment = first_coord.get_y() == second_coord.get_y();
  bool is_vertical_segment = first_coord.get_x() == second_coord.get_x();
  if (!is_horizontal_segment && !is_vertical_segment) {
    RTLOG.warn(Loc::current(), "The wire segment is not horizontal or vertical!");
    return GRParams::kInf;
  }
  GRWireEdgeIter iter(grid, LayerCoord(first_coord, layer_idx), LayerCoord(second_coord, layer_idx));
  if (!iter.isLegal()) {
    return GRParams::kInf;
  }
  double total_cost = 0.0;
  for (; iter.isValid(); iter.next()) {
    double one_edge_cost = edgeCost(grid, net, params, iter.getEdgeId());
    if (one_edge_cost >= GRParams::kInf) {
      return GRParams::kInf;
    }
    total_cost += one_edge_cost;
  }
  return total_cost;
}

double wireMoveCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, int32_t x, int32_t y, bool is_horizontal)
{
  double total_supply = 0.0;
  double total_demand = 0.0;
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
  }
  if (!has_edge || total_supply <= GRParams::kEpsilon) {
    return GRParams::kInf;
  }
  return params.unit_wire_cost + params.cost_weight * congestionCost(params, total_supply, total_demand, 1.0);
}

double treeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRTree& tree)
{
  double total_cost = 0.0;
  for (const GRWireSegment& wire_segment : tree.wire_list) {
    if (wire_segment.first.get_layer_idx() != wire_segment.second.get_layer_idx()) {
      continue;
    }
    double wire_cost = segmentCost(grid, net, params, wire_segment.first.get_layer_idx(), wire_segment.first, wire_segment.second);
    if (wire_cost >= GRParams::kInf) {
      return GRParams::kInf;
    }
    total_cost += wire_cost;
  }
  for (const GRViaStack& via_stack : tree.via_list) {
    if (via_stack.bottom_layer_idx < grid.getBottomLayerIdx() || via_stack.top_layer_idx > grid.getTopLayerIdx()
        || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      continue;
    }
    for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx < via_stack.top_layer_idx; layer_idx++) {
      double one_via_cost = viaCost(grid, net, params, via_stack.coord, layer_idx);
      if (one_via_cost >= GRParams::kInf) {
        return GRParams::kInf;
      }
      total_cost += one_via_cost;
    }
  }
  return total_cost;
}

double treeOverflow(const GRGridGraph& grid, const GRNet& net, const GRTree& tree)
{
  return net.getOverflow(grid, tree);
}

bool isBetterResult(double old_overflow, double old_cost, double new_overflow, double new_cost)
{
  if (new_overflow < old_overflow - GRParams::kEpsilon) {
    return true;
  }
  if (new_overflow > old_overflow + GRParams::kEpsilon) {
    return false;
  }
  return new_cost < old_cost - GRParams::kEpsilon;
}

}  // namespace irt
