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

#include "Net.hpp"
#include "PRPin.hpp"
#include "PlanarCoord.hpp"
#include "RoutingEdge.hpp"
#include "Segment.hpp"

namespace irt {

class PRNet
{
 public:
  PRNet() = default;
  ~PRNet() = default;
  // getter
  Net* get_origin_net() { return _origin_net; }
  int32_t get_net_idx() const { return _net_idx; }
  ConnectType& get_connect_type() { return _connect_type; }
  std::vector<PRPin>& get_pr_pin_list() { return _pr_pin_list; }
  BoundingBox& get_bounding_box() { return _bounding_box; }
  std::vector<Segment<PlanarCoord>>& get_routing_segment_list() { return _routing_segment_list; }
  std::unordered_set<RoutingEdge*>& get_routing_edge_set() { return _routing_edge_set; }
  // const getter
  const ConnectType& get_connect_type() const { return _connect_type; }
  const std::vector<PRPin>& get_pr_pin_list() const { return _pr_pin_list; }
  const BoundingBox& get_bounding_box() const { return _bounding_box; }
  const std::unordered_set<RoutingEdge*>& get_routing_edge_set() const { return _routing_edge_set; }
  // setter
  void set_origin_net(Net* origin_net) { _origin_net = origin_net; }
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_connect_type(const ConnectType& connect_type) { _connect_type = connect_type; }
  void set_pr_pin_list(const std::vector<PRPin>& pr_pin_list) { _pr_pin_list = pr_pin_list; }
  void set_bounding_box(const BoundingBox& bounding_box) { _bounding_box = bounding_box; }
  void set_routing_segment_list(const std::vector<Segment<PlanarCoord>>& routing_segment_list) { _routing_segment_list = routing_segment_list; }
  void set_routing_segment_list(std::vector<Segment<PlanarCoord>>&& routing_segment_list) { _routing_segment_list = std::move(routing_segment_list); }
  // function

 private:
  Net* _origin_net = nullptr;
  int32_t _net_idx = -1;
  ConnectType _connect_type = ConnectType::kNone;
  std::vector<PRPin> _pr_pin_list;
  BoundingBox _bounding_box;
  std::vector<Segment<PlanarCoord>> _routing_segment_list;
  std::unordered_set<RoutingEdge*> _routing_edge_set;
};

struct CmpPRNet
{
  bool operator()(const PRNet* first_net, const PRNet* second_net) const
  {
    // 时钟线网优先
    bool first_is_clock = first_net->get_connect_type() == ConnectType::kClock;
    bool second_is_clock = second_net->get_connect_type() == ConnectType::kClock;
    if (first_is_clock != second_is_clock) {
      return first_is_clock;
    }
    // BoundingBox 大小升序
    double first_total_size = first_net->get_bounding_box().getTotalSize();
    double second_total_size = second_net->get_bounding_box().getTotalSize();
    if (first_total_size != second_total_size) {
      return first_total_size < second_total_size;
    }
    // 长宽比 降序
    double first_length_width_ratio = first_net->get_bounding_box().getXSize() / 1.0 / first_net->get_bounding_box().getYSize();
    if (first_length_width_ratio < 1) {
      first_length_width_ratio = 1 / first_length_width_ratio;
    }
    double second_length_width_ratio = second_net->get_bounding_box().getXSize() / 1.0 / second_net->get_bounding_box().getYSize();
    if (second_length_width_ratio < 1) {
      second_length_width_ratio = 1 / second_length_width_ratio;
    }
    if (first_length_width_ratio != second_length_width_ratio) {
      return first_length_width_ratio > second_length_width_ratio;
    }
    // PinNum 降序
    int32_t first_pin_num = static_cast<int32_t>(first_net->get_pr_pin_list().size());
    int32_t second_pin_num = static_cast<int32_t>(second_net->get_pr_pin_list().size());
    return first_pin_num > second_pin_num;
  }
};

}  // namespace irt
