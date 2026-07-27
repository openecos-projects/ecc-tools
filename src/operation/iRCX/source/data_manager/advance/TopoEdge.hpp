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

#include "LineSegment.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class TopoEdge
{
 public:
  TopoEdge() = default;
  explicit TopoEdge(int32_t net_idx) : _net_idx(net_idx) {}
  ~TopoEdge() = default;
  // getter
  int32_t get_edge_idx() const { return _edge_idx; }
  int32_t get_net_idx() const { return _net_idx; }
  std::string& get_via_name() { return _via_name; }
  int32_t get_start_node_idx() const { return _start_node_idx; }
  int32_t get_end_node_idx() const { return _end_node_idx; }
  int32_t get_layer_idx() const { return _layer_idx; }
  GTLRectInt& get_shape() { return _shape; }
  int32_t get_width() const { return _width; }
  int32_t get_half_width() const { return _half_width; }
  int32_t get_length() const { return _length; }
  GTLPointInt& get_center() { return _center; }
  LineSegment& get_line_segment() { return _line_segment; }
  // setter
  void set_via_name(const std::string& via_name) { _via_name = via_name; }
  void set_start_node_idx(int32_t start_node_idx) { _start_node_idx = start_node_idx; }
  void set_end_node_idx(int32_t end_node_idx) { _end_node_idx = end_node_idx; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_shape(const GTLRectInt& shape)
  {
    _shape = shape;

    int32_t lower_x = boost::polygon::xl(_shape);
    int32_t lower_y = boost::polygon::yl(_shape);
    int32_t upper_x = boost::polygon::xh(_shape);
    int32_t upper_y = boost::polygon::yh(_shape);
    int32_t x_span = upper_x - lower_x;
    int32_t y_span = upper_y - lower_y;
    bool is_horizontal = x_span >= y_span;

    _width = is_horizontal ? y_span : x_span;
    _half_width = _width / 2;
    _length = is_horizontal ? x_span : y_span;
    _center = GTLPointInt(lower_x + x_span / 2, lower_y + y_span / 2);

    _line_segment.set_is_horizontal(is_horizontal);
    _line_segment.set_coord(is_horizontal ? lower_y + y_span / 2 : lower_x + x_span / 2);
    _line_segment.set_lower(is_horizontal ? lower_x : lower_y);
    _line_segment.set_upper(is_horizontal ? upper_x : upper_y);
  }
  // function
  bool get_is_via() const { return !_via_name.empty(); }
  bool get_is_special_net() const { return _is_special_net; }

 private:
  friend class TopoPool;

  void set_edge_idx(int32_t edge_idx) { _edge_idx = edge_idx; }
  void set_is_special_net(bool is_special_net) { _is_special_net = is_special_net; }

  int32_t _edge_idx = -1;
  int32_t _net_idx = -1;
  bool _is_special_net = false;
  std::string _via_name;
  int32_t _start_node_idx = -1;
  int32_t _end_node_idx = -1;
  int32_t _layer_idx = -1;
  GTLRectInt _shape;
  int32_t _width = -1;
  int32_t _half_width = -1;
  int32_t _length = -1;
  GTLPointInt _center;
  LineSegment _line_segment;
};

}  // namespace ircx
