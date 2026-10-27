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
#include <set>
#include <utility>
#include <vector>

#include "BoundingBox.hpp"
#include "GRGridGraph.hpp"
#include "LayerCoord.hpp"

namespace irt {

class Net;
class Pin;

struct GRPin
{
  Pin* pin = nullptr;
  PlanarCoord grid_coord;
  int32_t layer_idx = -1;
};

struct GRWireSegment
{
  LayerCoord first;
  LayerCoord second;
};

struct GRViaStack
{
  PlanarCoord coord;
  int32_t bottom_layer_idx = -1;
  int32_t top_layer_idx = -1;
};

struct GRTree
{
  std::vector<GRWireSegment> wire_list;
  std::vector<GRViaStack> via_list;
};

class GRNet
{
 public:
  GRNet() = default;

  void init(Net& net, const GRGridGraph& grid);

  Net* get_origin_net() const { return origin_net_; }
  const BoundingBox& get_bbox() const { return bbox_; }
  const std::vector<GRPin>& get_pin_list() const { return pin_list_; }
  bool isClock() const { return is_clock_; }
  const GRTree& get_tree() const { return tree_; }
  void set_tree(GRTree tree) { tree_ = std::move(tree); }
  // 取走已提交结果, 供单网协议在 ripup 之后暂存; 取走后 tree_ 为空
  GRTree release_tree() { return std::move(tree_); }

  bool isExempt(const GRWireEdgeId& edge_id) const { return exempt_edge_set_.contains(edge_id); }
  bool isAPViaSide(const PlanarCoord& coord, int32_t layer_idx) const;

  void commitUsage(GRGridGraph& grid, const GRParams& params, const GRTree& tree) const;
  void ripupUsage(GRGridGraph& grid, const GRParams& params, const GRTree& tree) const;
  bool hasOverflow(const GRGridGraph& grid) const;

 private:
  void buildExemptEdgeSet(const GRGridGraph& grid);
  void addPinEscapeCorridor(const GRGridGraph& grid, const GRPin& gr_pin, bool is_horizontal, int32_t direction);
  GRWireEdgeId getEscapeEdgeId(int32_t x, int32_t y, bool is_horizontal, int32_t direction, int32_t layer_idx) const;

  void buildPlanarEdgeSet(const GRGridGraph& grid, const GRTree& tree, std::set<GRWireEdgeId>& planar_edge_set) const;
  void addWireDemand(GRGridGraph& grid, const GRTree& tree, std::set<GRWireEdgeId>& charged_edge_set, double delta) const;
  void addViaDemand(GRGridGraph& grid, const GRViaStack& via_stack, const std::set<GRWireEdgeId>& planar_edge_set, std::set<GRWireEdgeId>& charged_edge_set,
                    double delta) const;
  bool hasPlanarEdgeOrAP(const GRGridGraph& grid, const std::set<GRWireEdgeId>& planar_edge_set, const PlanarCoord& coord, int32_t layer_idx) const;

  Net* origin_net_ = nullptr;
  BoundingBox bbox_;
  std::vector<GRPin> pin_list_;
  bool is_clock_ = false;
  std::set<GRWireEdgeId> exempt_edge_set_;
  GRTree tree_;
};

// 线网调度顺序, 只读 GRNet 自己保存的字段
struct CmpGRNet
{
  bool operator()(const GRNet& first, const GRNet& second) const
  {
    // 时钟线网优先
    if (first.isClock() != second.isClock()) {
      return first.isClock();
    }
    const BoundingBox& first_bbox = first.get_bbox();
    const BoundingBox& second_bbox = second.get_bbox();
    // BoundingBox 大小升序
    double first_total_size = first_bbox.getTotalSize();
    double second_total_size = second_bbox.getTotalSize();
    if (first_total_size != second_total_size) {
      return first_total_size < second_total_size;
    }
    // 长宽比降序
    double first_length_width_ratio = first_bbox.getXSize() / std::max(1.0, static_cast<double>(first_bbox.getYSize()));
    if (first_length_width_ratio < 1.0) {
      first_length_width_ratio = 1.0 / first_length_width_ratio;
    }
    double second_length_width_ratio = second_bbox.getXSize() / std::max(1.0, static_cast<double>(second_bbox.getYSize()));
    if (second_length_width_ratio < 1.0) {
      second_length_width_ratio = 1.0 / second_length_width_ratio;
    }
    if (first_length_width_ratio != second_length_width_ratio) {
      return first_length_width_ratio > second_length_width_ratio;
    }
    // Pin 数降序
    return first.get_pin_list().size() > second.get_pin_list().size();
  }
};

}  // namespace irt
