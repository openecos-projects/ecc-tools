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

#include "LAPillar.hpp"
#include "LAPin.hpp"
#include "Net.hpp"

namespace irt {

class LANet
{
 public:
  LANet() = default;
  ~LANet() = default;
  // getter
  Net* get_origin_net() { return _origin_net; }
  int32_t get_net_idx() const { return _net_idx; }
  ConnectType& get_connect_type() { return _connect_type; }
  std::vector<LAPin>& get_la_pin_list() { return _la_pin_list; }
  BoundingBox& get_bounding_box() { return _bounding_box; }
  MTree<LayerCoord>& get_planar_tree() { return _planar_tree; }
  MTree<LAPillar>& get_pillar_tree() { return _pillar_tree; }
  // const getter
  const ConnectType& get_connect_type() const { return _connect_type; }
  const std::vector<LAPin>& get_la_pin_list() const { return _la_pin_list; }
  const BoundingBox& get_bounding_box() const { return _bounding_box; }
  // setter
  void set_origin_net(Net* origin_net) { _origin_net = origin_net; }
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_connect_type(const ConnectType& connect_type) { _connect_type = connect_type; }
  void set_bounding_box(const BoundingBox& bounding_box) { _bounding_box = bounding_box; }
  void set_planar_tree(MTree<LayerCoord>&& planar_tree) { _planar_tree = std::move(planar_tree); }
  void set_pillar_tree(MTree<LAPillar>&& pillar_tree) { _pillar_tree = std::move(pillar_tree); }
  // function

 private:
  Net* _origin_net = nullptr;
  int32_t _net_idx = -1;
  ConnectType _connect_type = ConnectType::kNone;
  std::vector<LAPin> _la_pin_list;
  BoundingBox _bounding_box;
  MTree<LayerCoord> _planar_tree;
  MTree<LAPillar> _pillar_tree;
};

struct CmpLANet
{
  bool operator()(const LANet* a, const LANet* b) const
  {
    bool a_is_clock = a->get_connect_type() == ConnectType::kClock;
    bool b_is_clock = b->get_connect_type() == ConnectType::kClock;
    if (a_is_clock != b_is_clock) {
      return a_is_clock;
    }
    double a_total_size = a->get_bounding_box().getTotalSize();
    double b_total_size = b->get_bounding_box().getTotalSize();
    if (a_total_size != b_total_size) {
      return a_total_size < b_total_size;
    }
    double a_length_width_ratio = a->get_bounding_box().getXSize() / 1.0 / a->get_bounding_box().getYSize();
    if (a_length_width_ratio < 1) {
      a_length_width_ratio = 1 / a_length_width_ratio;
    }
    double b_length_width_ratio = b->get_bounding_box().getXSize() / 1.0 / b->get_bounding_box().getYSize();
    if (b_length_width_ratio < 1) {
      b_length_width_ratio = 1 / b_length_width_ratio;
    }
    if (a_length_width_ratio != b_length_width_ratio) {
      return a_length_width_ratio > b_length_width_ratio;
    }
    return a->get_la_pin_list().size() > b->get_la_pin_list().size();
  }
};

}  // namespace irt
