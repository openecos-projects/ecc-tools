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
#pragma once

#include "Database.hpp"
#include "GridMap.hpp"
#include "PlanarCoord.hpp"

namespace irt {

struct GRWireEdgeId
{
  int32_t x = -1;
  int32_t y = -1;
  int32_t layer_idx = -1;

  bool operator<(const GRWireEdgeId& other) const
  {
    if (layer_idx != other.layer_idx) {
      return layer_idx < other.layer_idx;
    }
    if (x != other.x) {
      return x < other.x;
    }
    return y < other.y;
  }
};

struct GRWireEdge
{
  double supply = 0.0;
  double fixed_demand = 0.0;
  double routing_demand = 0.0;

  double getDemand() const { return fixed_demand + routing_demand; }
  double getOverflow() const { return std::max(0.0, getDemand() - supply); }
};

class GRGridGraph
{
 public:
  GRGridGraph() = default;
  ~GRGridGraph() = default;

  void build(Database& database, int32_t bottom_layer_idx, int32_t top_layer_idx);

  int32_t getXSize() const { return _x_size; }
  int32_t getYSize() const { return _y_size; }
  int32_t getLayerNum() const { return _layer_num; }
  int32_t getBottomLayerIdx() const { return _bottom_layer_idx; }
  int32_t getTopLayerIdx() const { return _top_layer_idx; }
  bool isHorizontalLayer(int32_t layer_idx) const { return _is_horizontal_layer[layer_idx]; }

  bool hasWireEdge(const GRWireEdgeId& edge_id) const;
  const GRWireEdge& getWireEdge(const GRWireEdgeId& edge_id) const;
  GRWireEdge& getWireEdge(const GRWireEdgeId& edge_id);
  const GridMap<GRWireEdge>& getEdgeMap(int32_t layer_idx) const { return _edge_map[layer_idx]; }

 private:
  int32_t _x_size = 0;
  int32_t _y_size = 0;
  int32_t _layer_num = 0;
  int32_t _bottom_layer_idx = -1;
  int32_t _top_layer_idx = -1;
  std::vector<bool> _is_horizontal_layer;
  std::vector<GridMap<GRWireEdge>> _edge_map;
};

}  // namespace irt
