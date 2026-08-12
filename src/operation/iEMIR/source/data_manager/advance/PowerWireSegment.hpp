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

class PowerWireSegment
{
 public:
  PowerWireSegment() = default;
  ~PowerWireSegment() = default;
  // getter
  int32_t get_layer_idx() { return _layer_idx; }
  std::string& get_layer_name() { return _layer_name; }
  int32_t get_first_x() { return _first_x; }
  int32_t get_first_y() { return _first_y; }
  int32_t get_second_x() { return _second_x; }
  int32_t get_second_y() { return _second_y; }
  int32_t get_width() { return _width; }
  double get_resistance_per_square() { return _resistance_per_square; }
  // setter
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_first_x(int32_t first_x) { _first_x = first_x; }
  void set_first_y(int32_t first_y) { _first_y = first_y; }
  void set_second_x(int32_t second_x) { _second_x = second_x; }
  void set_second_y(int32_t second_y) { _second_y = second_y; }
  void set_width(int32_t width) { _width = width; }
  void set_resistance_per_square(double resistance_per_square) { _resistance_per_square = resistance_per_square; }
  // function

 private:
  int32_t _layer_idx = -1;
  std::string _layer_name;
  int32_t _first_x = 0;
  int32_t _first_y = 0;
  int32_t _second_x = 0;
  int32_t _second_y = 0;
  int32_t _width = 0;
  double _resistance_per_square = 0.0;
};

}  // namespace iemir
