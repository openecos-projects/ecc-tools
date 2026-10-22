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
#include <climits>
#include <cstdint>
#include <set>
#include <vector>

#include "BoundingBox.hpp"
#include "GRGridGraph.hpp"
#include "LayerCoord.hpp"
#include "Net.hpp"
#include "Pin.hpp"

namespace irt {

struct GRPin
{
  Pin* pin = nullptr;
  PlanarCoord grid_coord;
  int32_t layer_idx = -1;
};

struct GRTopoNode
{
  PlanarCoord coord;
  int32_t fixed_low = INT_MAX;
  int32_t fixed_high = INT_MIN;
  int32_t parent = -1;
  bool is_bend = false;
  std::vector<int32_t> child_list;
  std::vector<std::vector<int32_t>> child_bend_list;  // parallel to child_list, stores the first node of each candidate pattern chain
};

struct GRTopoTree
{
  std::vector<GRTopoNode> node_list;
  int32_t root_idx = -1;
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

struct GRNet
{
  Net* origin_net = nullptr;
  BoundingBox bbox;
  std::vector<GRPin> pin_list;
  std::set<GRWireEdgeId> exempt_edge_set;
  GRTopoTree topology;
  GRTree routing_tree;
};

struct CmpGRNet
{
  bool operator()(const GRNet& first, const GRNet& second) const
  {
    Net& first_net = *(first.origin_net);
    Net& second_net = *(second.origin_net);
    BoundingBox& first_bbox = first_net.get_bounding_box();
    BoundingBox& second_bbox = second_net.get_bounding_box();
    // 时钟线网优先
    bool first_is_clock = first_net.get_connect_type() == ConnectType::kClock;
    bool second_is_clock = second_net.get_connect_type() == ConnectType::kClock;
    if (first_is_clock != second_is_clock) {
      return first_is_clock;
    }
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
    return first_net.get_pin_list().size() > second_net.get_pin_list().size();
  }
};

}  // namespace irt
