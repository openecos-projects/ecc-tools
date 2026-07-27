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

class PowerVia
{
 public:
  PowerVia() = default;
  ~PowerVia() = default;
  // getter
  int32_t get_bottom_layer_idx() { return _bottom_layer_idx; }
  int32_t get_top_layer_idx() { return _top_layer_idx; }
  int32_t get_x() { return _x; }
  int32_t get_y() { return _y; }
  int32_t get_cut_num() { return _cut_num; }
  double get_resistance() { return _resistance; }
  // setter
  void set_bottom_layer_idx(int32_t bottom_layer_idx) { _bottom_layer_idx = bottom_layer_idx; }
  void set_top_layer_idx(int32_t top_layer_idx) { _top_layer_idx = top_layer_idx; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_cut_num(int32_t cut_num) { _cut_num = cut_num; }
  void set_resistance(double resistance) { _resistance = resistance; }
  // function

 private:
  int32_t _bottom_layer_idx = -1;
  int32_t _top_layer_idx = -1;
  int32_t _x = 0;
  int32_t _y = 0;
  int32_t _cut_num = 0;
  double _resistance = 0.0;
};

}  // namespace iemir
