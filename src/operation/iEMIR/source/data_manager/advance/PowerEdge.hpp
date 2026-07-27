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

#include "EMIRHeader.hpp"
#include "PowerEdgeType.hpp"

namespace iemir {

class PowerEdge
{
 public:
  PowerEdge() = default;
  ~PowerEdge() = default;
  // getter
  std::size_t get_edge_id() { return _edge_id; }
  PowerEdgeType get_type() { return _type; }
  std::size_t get_first_node_id() { return _first_node_id; }
  std::size_t get_second_node_id() { return _second_node_id; }
  int32_t get_layer_idx() { return _layer_idx; }
  int32_t get_width() { return _width; }
  int32_t get_length() { return _length; }
  double get_resistance() { return _resistance; }
  double get_current() { return _current; }
  double get_current_density() { return _current_density; }
  bool get_is_violation() { return _is_violation; }
  // setter
  void set_edge_id(std::size_t edge_id) { _edge_id = edge_id; }
  void set_type(PowerEdgeType type) { _type = type; }
  void set_first_node_id(std::size_t first_node_id) { _first_node_id = first_node_id; }
  void set_second_node_id(std::size_t second_node_id) { _second_node_id = second_node_id; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_width(int32_t width) { _width = width; }
  void set_length(int32_t length) { _length = length; }
  void set_resistance(double resistance) { _resistance = resistance; }
  void set_current(double current) { _current = current; }
  void set_current_density(double current_density) { _current_density = current_density; }
  void set_is_violation(bool is_violation) { _is_violation = is_violation; }
  // function

 private:
  std::size_t _edge_id = 0;
  PowerEdgeType _type = PowerEdgeType::kNone;
  std::size_t _first_node_id = 0;
  std::size_t _second_node_id = 0;
  int32_t _layer_idx = -1;
  int32_t _width = 0;
  int32_t _length = 0;
  double _resistance = 0.0;
  double _current = 0.0;
  double _current_density = 0.0;
  bool _is_violation = false;
};

}  // namespace iemir
