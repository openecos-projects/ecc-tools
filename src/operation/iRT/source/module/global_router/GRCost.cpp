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
#include <cstdint>
#include <set>

#include "Logger.hpp"

namespace irt {

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
  return amount * (params.unit_wire_cost + params.cost_weight * congestionCost(params, usable_supply, edge.getDemand(), amount) + edge.congestion_cost);
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
  double saturation_start_ratio = params.congestion_saturation_ratio;
  if (usage_ratio < saturation_start_ratio) {
    return unit_cost;
  }
  double saturation_ratio = (usage_ratio - saturation_start_ratio) / (1.0 - saturation_start_ratio);
  unit_cost += saturation_ratio * saturation_ratio;
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
    // 线段方向与该层首选方向不一致
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

bool isBetter(double old_overflow, double old_cost, double new_overflow, double new_cost)
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
