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
#include "GRSupplyTrial.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Logger.hpp"

namespace irt {

void GRSupplyTrial::capture(const GRGridGraph& grid)
{
  usage_list_.clear();
  usage_list_.resize(grid.getLayerNum());
  for (int32_t layer_idx = grid.getBottomLayerIdx(); layer_idx <= grid.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = grid.getEdgeMap(layer_idx);
    GridMap<double>& usage_map = usage_list_[layer_idx];
    usage_map.init(edge_map.get_x_size(), edge_map.get_y_size(), 0.0);
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        const GRWireEdge& edge = edge_map[x][y];
        double base_supply = edge.getBaseSupply();
        if (base_supply <= GRParams::kEpsilon) {
          continue;
        }
        usage_map[x][y] = edge.getDemand() / base_supply;
      }
    }
  }
}

void GRSupplyTrial::smooth(const GRParams& params)
{
  int32_t radius = std::max(0, params.trial_smooth_radius);
  double neighbor_ratio = std::clamp(params.trial_smooth_neighbor_ratio, 0.0, 1.0);

  smoothed_list_.clear();
  smoothed_list_.resize(usage_list_.size());
  for (size_t layer_idx = 0; layer_idx < usage_list_.size(); layer_idx++) {
    const GridMap<double>& usage_map = usage_list_[layer_idx];
    GridMap<double>& smoothed_map = smoothed_list_[layer_idx];
    if (usage_map.empty()) {
      continue;
    }
    smoothed_map.init(usage_map.get_x_size(), usage_map.get_y_size(), 0.0);
    if (radius == 0) {
      smoothed_map = usage_map;
      continue;
    }
    GridMap<double> x_smoothed_map(usage_map.get_x_size(), usage_map.get_y_size(), 0.0);
    for (int32_t y = 0; y < usage_map.get_y_size(); y++) {
      for (int32_t x = 0; x < usage_map.get_x_size(); x++) {
        double usage_sum = 0.0;
        int32_t usage_num = 0;
        for (int32_t offset = -radius; offset <= radius; offset++) {
          int32_t neighbor_x = x + offset;
          if (neighbor_x < 0 || neighbor_x >= usage_map.get_x_size()) {
            continue;
          }
          usage_sum += usage_map[neighbor_x][y];
          usage_num++;
        }
        x_smoothed_map[x][y] = usage_sum / usage_num;
      }
    }
    for (int32_t x = 0; x < usage_map.get_x_size(); x++) {
      for (int32_t y = 0; y < usage_map.get_y_size(); y++) {
        double usage_sum = 0.0;
        int32_t usage_num = 0;
        for (int32_t offset = -radius; offset <= radius; offset++) {
          int32_t neighbor_y = y + offset;
          if (neighbor_y < 0 || neighbor_y >= usage_map.get_y_size()) {
            continue;
          }
          usage_sum += x_smoothed_map[x][neighbor_y];
          usage_num++;
        }
        double smooth_usage = usage_sum / usage_num;
        smoothed_map[x][y] = neighbor_ratio * smooth_usage + (1.0 - neighbor_ratio) * usage_map[x][y];
      }
    }
  }
}

void GRSupplyTrial::applyReserve(GRGridGraph& grid, const GRParams& params) const
{
  if (smoothed_list_.size() != usage_list_.size()) {
    RTLOG.error(Loc::current(), "The supply trial is not captured and smoothed!");
  }

  size_t reserved_edge_base = 0;
  size_t reserved_edge_num = 0;
  for (int32_t layer_idx = grid.getBottomLayerIdx(); layer_idx <= grid.getTopLayerIdx(); layer_idx++) {
    const GridMap<GRWireEdge>& edge_map = grid.getEdgeMap(layer_idx);
    const GridMap<double>& smoothed_map = smoothed_list_[layer_idx];
    for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
        const GRWireEdge& edge = edge_map[x][y];
        if (edge.getBaseSupply() <= GRParams::kEpsilon) {
          continue;
        }
        reserved_edge_base++;
        if (getReserveTrack(edge, smoothed_map[x][y], params) <= 0.0) {
          continue;
        }
        grid.addReserved(GRWireEdgeId{x, y, layer_idx}, 1.0);
        reserved_edge_num++;
      }
    }
  }
  RTLOG.info(Loc::current(), "Trial reserved edge num: ", reserved_edge_num, "/", reserved_edge_base);
}

double GRSupplyTrial::getReserveTrack(const GRWireEdge& edge, double smoothed_usage, const GRParams& params) const
{
  double base_supply = edge.getBaseSupply();
  if (base_supply <= GRParams::kEpsilon) {
    return 0.0;
  }
  if (smoothed_usage <= params.trial_reserve_start_ratio + GRParams::kEpsilon) {
    return 0.0;
  }
  if (params.trial_keep_last_track && base_supply <= 1.0 + GRParams::kEpsilon) {
    return 0.0;
  }
  return 1.0;
}

}  // namespace irt
