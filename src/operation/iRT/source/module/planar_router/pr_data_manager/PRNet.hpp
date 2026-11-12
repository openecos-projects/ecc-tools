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
#include "PlanarCoord.hpp"
#include "PRGridGraph.hpp"
#include "Segment.hpp"

namespace irt {

class PRNet
{
 public:
  PRNet() = default;
  ~PRNet() = default;
  // getter
  Net* get_origin_net() const { return _origin_net; }
  int32_t get_net_idx() const { return _origin_net->get_net_idx(); }
  std::string& get_net_name() { return _origin_net->get_net_name(); }
  std::vector<Pin>& get_pin_list() { return _origin_net->get_pin_list(); }
  std::vector<Segment<PlanarCoord>>& get_routing_segment_list() { return _routing_segment_list; }
  PREdgeSet& get_routing_edge_set() { return _routing_edge_set; }
  // const getter
  const std::vector<PlanarCoord>& get_node_coord_list() const { return _node_coord_list; }
  const PREdgeSet& get_routing_edge_set() const { return _routing_edge_set; }
  // setter
  void set_origin_net(Net* origin_net) { _origin_net = origin_net; }
  void set_node_coord_list(std::vector<PlanarCoord>&& node_coord_list) { _node_coord_list = std::move(node_coord_list); }
  void set_routing_segment_list(const std::vector<Segment<PlanarCoord>>& routing_segment_list) { _routing_segment_list = routing_segment_list; }
  void set_routing_segment_list(std::vector<Segment<PlanarCoord>>&& routing_segment_list) { _routing_segment_list = std::move(routing_segment_list); }
  void set_routing_edge_set(PREdgeSet&& routing_edge_set) { _routing_edge_set = std::move(routing_edge_set); }
  // function

 private:
  Net* _origin_net = nullptr;
  std::vector<PlanarCoord> _node_coord_list;
  std::vector<Segment<PlanarCoord>> _routing_segment_list;
  PREdgeSet _routing_edge_set;
};

struct CmpPRNet
{
  bool operator()(const PRNet* first_pr_net, const PRNet* second_pr_net) const
  {
    Net& first_net = *(first_pr_net->get_origin_net());
    Net& second_net = *(second_pr_net->get_origin_net());
    // 时钟线网优先
    bool first_is_clock = first_net.get_connect_type() == ConnectType::kClock;
    bool second_is_clock = second_net.get_connect_type() == ConnectType::kClock;
    if (first_is_clock != second_is_clock) {
      return first_is_clock;
    }
    // BoundingBox 大小升序
    double first_total_size = first_net.get_bounding_box().getTotalSize();
    double second_total_size = second_net.get_bounding_box().getTotalSize();
    if (first_total_size != second_total_size) {
      return first_total_size < second_total_size;
    }
    // 长宽比 降序
    double first_length_width_ratio = first_net.get_bounding_box().getXSize() / 1.0 / first_net.get_bounding_box().getYSize();
    if (first_length_width_ratio < 1) {
      first_length_width_ratio = 1 / first_length_width_ratio;
    }
    double second_length_width_ratio = second_net.get_bounding_box().getXSize() / 1.0 / second_net.get_bounding_box().getYSize();
    if (second_length_width_ratio < 1) {
      second_length_width_ratio = 1 / second_length_width_ratio;
    }
    if (first_length_width_ratio != second_length_width_ratio) {
      return first_length_width_ratio > second_length_width_ratio;
    }
    // PinNum 降序
    int32_t first_pin_num = static_cast<int32_t>(first_net.get_pin_list().size());
    int32_t second_pin_num = static_cast<int32_t>(second_net.get_pin_list().size());
    return first_pin_num > second_pin_num;
  }
};

}  // namespace irt
