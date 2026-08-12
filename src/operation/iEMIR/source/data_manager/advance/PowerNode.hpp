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
#include "PowerNodeType.hpp"

namespace iemir {

class PowerNode
{
 public:
  PowerNode() = default;
  ~PowerNode() = default;
  // getter
  std::size_t get_node_id() { return _node_id; }
  PowerNodeType get_type() { return _type; }
  int32_t get_layer_idx() { return _layer_idx; }
  int32_t get_x() { return _x; }
  int32_t get_y() { return _y; }
  bool get_is_source() { return _is_source; }
  std::set<uint64_t>& get_instance_id_set() { return _instance_id_set; }
  double get_voltage() { return _voltage; }
  double get_current() { return _current; }
  // setter
  void set_node_id(std::size_t node_id) { _node_id = node_id; }
  void set_type(PowerNodeType type) { _type = type; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_is_source(bool is_source) { _is_source = is_source; }
  void set_voltage(double voltage) { _voltage = voltage; }
  void set_current(double current) { _current = current; }
  // function

 private:
  std::size_t _node_id = 0;
  PowerNodeType _type = PowerNodeType::kNone;
  int32_t _layer_idx = -1;
  int32_t _x = 0;
  int32_t _y = 0;
  bool _is_source = false;
  std::set<uint64_t> _instance_id_set;
  double _voltage = 0.0;
  double _current = 0.0;
};

}  // namespace iemir
