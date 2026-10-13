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

namespace iemir {

class PowerPin
{
 public:
  PowerPin() = default;
  ~PowerPin() = default;
  // getter
  uint64_t get_instance_id() { return _instance_id; }
  std::string& get_pin_name() { return _pin_name; }
  int32_t get_layer_idx() { return _layer_idx; }
  int32_t get_x() { return _x; }
  int32_t get_y() { return _y; }
  bool has_bounds() { return _has_bounds; }
  int32_t get_ll_x() { return _ll_x; }
  int32_t get_ll_y() { return _ll_y; }
  int32_t get_ur_x() { return _ur_x; }
  int32_t get_ur_y() { return _ur_y; }
  bool has_port_id() { return _has_port_id; }
  uint64_t get_port_id() { return _port_id; }
  bool get_is_source() { return _is_source; }
  // setter
  void set_instance_id(uint64_t instance_id) { _instance_id = instance_id; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_bounds(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    _has_bounds = true;
    _ll_x = ll_x;
    _ll_y = ll_y;
    _ur_x = ur_x;
    _ur_y = ur_y;
    _x = ll_x + (ur_x - ll_x) / 2;
    _y = ll_y + (ur_y - ll_y) / 2;
  }
  void set_port_id(uint64_t port_id)
  {
    _has_port_id = true;
    _port_id = port_id;
  }
  void set_is_source(bool is_source) { _is_source = is_source; }
  // function

 private:
  uint64_t _instance_id = 0;
  std::string _pin_name;
  int32_t _layer_idx = -1;
  int32_t _x = 0;
  int32_t _y = 0;
  bool _has_bounds = false;
  int32_t _ll_x = 0;
  int32_t _ll_y = 0;
  int32_t _ur_x = 0;
  int32_t _ur_y = 0;
  bool _has_port_id = false;
  uint64_t _port_id = 0;
  bool _is_source = false;
};

}  // namespace iemir
