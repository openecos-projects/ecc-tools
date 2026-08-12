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
  bool get_is_source() { return _is_source; }
  // setter
  void set_instance_id(uint64_t instance_id) { _instance_id = instance_id; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_is_source(bool is_source) { _is_source = is_source; }
  // function

 private:
  uint64_t _instance_id = 0;
  std::string _pin_name;
  int32_t _layer_idx = -1;
  int32_t _x = 0;
  int32_t _y = 0;
  bool _is_source = false;
};

}  // namespace iemir
