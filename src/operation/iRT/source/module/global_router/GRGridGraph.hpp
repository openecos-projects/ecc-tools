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

#include <algorithm>
#include <cstdint>
#include <vector>

#include "GRParams.hpp"
#include "GridMap.hpp"
#include "LayerCoord.hpp"

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

  // fixed_demand is already removed from usable supply, so demand only counts routing demand.
  double getDemand() const { return routing_demand; }
  double getUsableSupply() const { return std::max(0.0, supply - fixed_demand); }
  double getOverflow() const { return std::max(0.0, getDemand() - getUsableSupply()); }
};

struct GRGridShape
{
  int32_t x_size = 0;
  int32_t y_size = 0;
  int32_t layer_num = 0;
  int32_t bottom_layer_idx = -1;
  int32_t top_layer_idx = -1;
  std::vector<bool> is_horizontal_layer;
};

struct GRLayerUsage
{
  double supply = 0.0;
  double demand = 0.0;
  double overflow = 0.0;
  double max_overflow = 0.0;
  int32_t overflow_edge_num = 0;
};

class GRGridGraph
{
 public:
  GRGridGraph() = default;

  void init(const GRGridShape& shape);
  void setSupply(const GRWireEdgeId& edge_id, int32_t supply_num, int32_t usable_num);
  void addDemand(const GRWireEdgeId& edge_id, double delta, const char* demand_type);

  int32_t getXSize() const { return x_size_; }
  int32_t getYSize() const { return y_size_; }
  int32_t getLayerNum() const { return layer_num_; }
  int32_t getBottomLayerIdx() const { return bottom_layer_idx_; }
  int32_t getTopLayerIdx() const { return top_layer_idx_; }
  bool isHorizontalLayer(int32_t layer_idx) const { return is_horizontal_layer_[layer_idx]; }

  bool hasWireEdge(const GRWireEdgeId& edge_id) const;
  bool hasUsableSupply(const GRWireEdgeId& edge_id) const { return hasWireEdge(edge_id) && getWireEdge(edge_id).getUsableSupply() > GRParams::kEpsilon; }
  const GRWireEdge& getWireEdge(const GRWireEdgeId& edge_id) const { return edge_map_[edge_id.layer_idx][edge_id.x][edge_id.y]; }
  GRWireEdge& getWireEdge(const GRWireEdgeId& edge_id) { return edge_map_[edge_id.layer_idx][edge_id.x][edge_id.y]; }
  const GridMap<GRWireEdge>& getEdgeMap(int32_t layer_idx) const { return edge_map_[layer_idx]; }

  double getTotalOverflow() const;
  GRLayerUsage getLayerUsage(int32_t layer_idx) const;

 private:
  int32_t x_size_ = 0;
  int32_t y_size_ = 0;
  int32_t layer_num_ = 0;
  int32_t bottom_layer_idx_ = -1;
  int32_t top_layer_idx_ = -1;
  std::vector<bool> is_horizontal_layer_;
  std::vector<GridMap<GRWireEdge>> edge_map_;
};

// 同层水平/垂直段覆盖的网格边, 半开区间 [begin, end)
class GRWireEdgeIter
{
 public:
  GRWireEdgeIter(const GRGridGraph& grid, const LayerCoord& first, const LayerCoord& second);

  // isLegal: 同层且方向与该层一致; 同点也算合法但为空
  bool isLegal() const { return is_legal_; }
  bool isValid() const { return is_legal_ && pos_ < end_; }
  const GRWireEdgeId& getEdgeId() const { return edge_id_; }
  void next();

 private:
  void updateEdgeId();

  bool is_legal_ = false;
  bool is_horizontal_ = false;
  int32_t layer_idx_ = -1;
  int32_t line_coord_ = 0;
  int32_t pos_ = 0;
  int32_t end_ = 0;
  GRWireEdgeId edge_id_;
};

// via 在某一层的两条候选投影边, 构造时已过滤掉不存在的边
class GRViaEdgeIter
{
 public:
  GRViaEdgeIter(const GRGridGraph& grid, const PlanarCoord& coord, int32_t layer_idx);

  bool isValid() const { return index_ < count_; }
  const GRWireEdgeId& getEdgeId() const { return candidate_list_[index_]; }
  void next() { ++index_; }

 private:
  GRWireEdgeId candidate_list_[2];
  int32_t count_ = 0;
  int32_t index_ = 0;
};

}  // namespace irt
