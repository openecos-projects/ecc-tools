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

#include "RCXHeader.hpp"

namespace ircx {

class TopoNode
{
 public:
  explicit TopoNode(int32_t net_idx) : _net_idx(net_idx) {}
  TopoNode() = delete;
  ~TopoNode() = default;
  // getter
  int32_t get_node_idx() const { return _node_idx; }
  int32_t get_net_idx() const { return _net_idx; }
  int32_t get_layer_idx() const { return _layer_idx; }
  bool get_is_shape_valid() const { return _is_shape_valid; }
  GTLPointInt& get_point() { return _point; }
  GTLRectInt& get_shape() { return _shape; }
  std::string& get_pin_name() { return _pin_name; }
  // setter
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_is_shape_valid(bool is_shape_valid) { _is_shape_valid = is_shape_valid; }
  void set_point(const GTLPointInt& point) { _point = point; }
  void set_shape(const GTLRectInt& shape) { _shape = shape; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  // function
  bool get_is_pin_node() const { return !_pin_name.empty(); }

 private:
  friend class TopoPool;

  void set_node_idx(int32_t node_idx) { _node_idx = node_idx; }

  int32_t _node_idx = -1;
  int32_t _net_idx = -1;
  int32_t _layer_idx = -1;
  bool _is_shape_valid = false;
  GTLPointInt _point;
  GTLRectInt _shape;
  std::string _pin_name;
};

}  // namespace ircx
