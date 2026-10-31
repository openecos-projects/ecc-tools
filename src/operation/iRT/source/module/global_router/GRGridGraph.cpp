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
#include "GRGridGraph.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Logger.hpp"

namespace irt {

void GRGridGraph::init(const GRGridShape& shape)
{
  x_size_ = shape.x_size;
  y_size_ = shape.y_size;
  layer_num_ = shape.layer_num;
  bottom_layer_idx_ = shape.bottom_layer_idx;
  top_layer_idx_ = shape.top_layer_idx;
  is_horizontal_layer_ = shape.is_horizontal_layer;

  edge_map_.resize(layer_num_);
  for (int32_t layer_idx = 0; layer_idx < layer_num_; layer_idx++) {
    if (is_horizontal_layer_[layer_idx]) {
      edge_map_[layer_idx].init(std::max(0, x_size_ - 1), y_size_);
    } else {
      edge_map_[layer_idx].init(x_size_, std::max(0, y_size_ - 1));
    }
  }
}

void GRGridGraph::setSupply(const GRWireEdgeId& edge_id, int32_t supply_num, int32_t usable_num)
{
  GRWireEdge& edge = getWireEdge(edge_id);
  edge.supply = static_cast<double>(supply_num);
  edge.fixed_demand = static_cast<double>(supply_num - usable_num);
}

void GRGridGraph::addDemand(const GRWireEdgeId& edge_id, double delta, const char* demand_type)
{
  GRWireEdge& edge = getWireEdge(edge_id);
  edge.routing_demand += delta;
  if (edge.routing_demand < -GRParams::kEpsilon) {
    RTLOG.error(Loc::current(), "The ", demand_type, " routing demand is negative!");
  }
  if (edge.routing_demand < 0.0) {
    edge.routing_demand = 0.0;
  }
}

bool GRGridGraph::hasWireEdge(const GRWireEdgeId& edge_id) const
{
  if (edge_id.layer_idx < 0 || edge_id.layer_idx >= layer_num_) {
    return false;
  }
  const GridMap<GRWireEdge>& edge_map = edge_map_[edge_id.layer_idx];
  return 0 <= edge_id.x && edge_id.x < edge_map.get_x_size() && 0 <= edge_id.y && edge_id.y < edge_map.get_y_size();
}

double GRGridGraph::getTotalOverflow() const
{
  double total_overflow = 0.0;
  for (int32_t layer_idx = bottom_layer_idx_; layer_idx <= top_layer_idx_; layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = edge_map_[layer_idx];
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        total_overflow += edge_map[x][y].getOverflow();
      }
    }
  }
  return total_overflow;
}

GRLayerUsage GRGridGraph::getLayerUsage(int32_t layer_idx) const
{
  GRLayerUsage layer_usage;
  const GridMap<GRWireEdge>& edge_map = edge_map_[layer_idx];
  for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
    for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
      const GRWireEdge& edge = edge_map[x][y];
      layer_usage.supply += edge.getUsableSupply();
      layer_usage.demand += edge.getDemand();
      double overflow = edge.getOverflow();
      layer_usage.overflow += overflow;
      layer_usage.max_overflow = std::max(layer_usage.max_overflow, overflow);
      if (overflow > 0.0) {
        layer_usage.overflow_edge_num++;
      }
    }
  }
  return layer_usage;
}

GRWireEdgeIter::GRWireEdgeIter(const GRGridGraph& grid, const LayerCoord& first, const LayerCoord& second)
{
  if (first.get_layer_idx() != second.get_layer_idx()) {
    return;
  }
  layer_idx_ = first.get_layer_idx();
  if (first.get_x() == second.get_x() && first.get_y() == second.get_y()) {
    // 同点: 合法但没有任何边
    is_legal_ = true;
    return;
  }
  if (first.get_y() == second.get_y()) {
    if (!grid.isHorizontalLayer(layer_idx_)) {
      return;
    }
    is_horizontal_ = true;
    line_coord_ = first.get_y();
    pos_ = std::min(first.get_x(), second.get_x());
    end_ = std::max(first.get_x(), second.get_x());
  } else if (first.get_x() == second.get_x()) {
    if (grid.isHorizontalLayer(layer_idx_)) {
      return;
    }
    line_coord_ = first.get_x();
    pos_ = std::min(first.get_y(), second.get_y());
    end_ = std::max(first.get_y(), second.get_y());
  } else {
    return;
  }
  is_legal_ = true;
  updateEdgeId();
}

void GRWireEdgeIter::next()
{
  ++pos_;
  updateEdgeId();
}

void GRWireEdgeIter::updateEdgeId()
{
  if (is_horizontal_) {
    edge_id_ = GRWireEdgeId{pos_, line_coord_, layer_idx_};
  } else {
    edge_id_ = GRWireEdgeId{line_coord_, pos_, layer_idx_};
  }
}

GRViaEdgeIter::GRViaEdgeIter(const GRGridGraph& grid, const PlanarCoord& coord, int32_t layer_idx)
{
  if (layer_idx < grid.getBottomLayerIdx() || layer_idx > grid.getTopLayerIdx()) {
    return;
  }
  bool is_horizontal = grid.isHorizontalLayer(layer_idx);
  for (int32_t offset = -1; offset <= 0; offset++) {
    GRWireEdgeId candidate
        = is_horizontal ? GRWireEdgeId{coord.get_x() + offset, coord.get_y(), layer_idx} : GRWireEdgeId{coord.get_x(), coord.get_y() + offset, layer_idx};
    if (grid.hasWireEdge(candidate)) {
      candidate_list_[count_++] = candidate;
    }
  }
}

}  // namespace irt
