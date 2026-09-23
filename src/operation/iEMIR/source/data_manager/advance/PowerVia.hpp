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
  std::string& get_via_name() { return _via_name; }
  std::string& get_bottom_layer_name() { return _bottom_layer_name; }
  std::string& get_top_layer_name() { return _top_layer_name; }
  int32_t get_bottom_layer_idx() { return _bottom_layer_idx; }
  int32_t get_top_layer_idx() { return _top_layer_idx; }
  int32_t get_x() { return _x; }
  int32_t get_y() { return _y; }
  int32_t get_bottom_x() { return _bottom_x; }
  int32_t get_bottom_y() { return _bottom_y; }
  int32_t get_top_x() { return _top_x; }
  int32_t get_top_y() { return _top_y; }
  int32_t get_cut_num() { return _cut_num; }
  int32_t get_cut_low_x() { return _cut_low_x; }
  int32_t get_cut_low_y() { return _cut_low_y; }
  int32_t get_cut_high_x() { return _cut_high_x; }
  int32_t get_cut_high_y() { return _cut_high_y; }
  double get_cut_area_um2() { return _cut_area_um2; }
  double get_resistance() { return _resistance; }
  // setter
  void set_via_name(const std::string& via_name) { _via_name = via_name; }
  void set_bottom_layer_name(const std::string& bottom_layer_name) { _bottom_layer_name = bottom_layer_name; }
  void set_top_layer_name(const std::string& top_layer_name) { _top_layer_name = top_layer_name; }
  void set_bottom_layer_idx(int32_t bottom_layer_idx) { _bottom_layer_idx = bottom_layer_idx; }
  void set_top_layer_idx(int32_t top_layer_idx) { _top_layer_idx = top_layer_idx; }
  void set_x(int32_t x)
  {
    _x = x;
    _bottom_x = x;
    _top_x = x;
  }
  void set_y(int32_t y)
  {
    _y = y;
    _bottom_y = y;
    _top_y = y;
  }
  void set_bottom_x(int32_t bottom_x) { _bottom_x = bottom_x; }
  void set_bottom_y(int32_t bottom_y) { _bottom_y = bottom_y; }
  void set_top_x(int32_t top_x) { _top_x = top_x; }
  void set_top_y(int32_t top_y) { _top_y = top_y; }
  void set_cut_num(int32_t cut_num) { _cut_num = cut_num; }
  void set_cut_low_x(int32_t cut_low_x) { _cut_low_x = cut_low_x; }
  void set_cut_low_y(int32_t cut_low_y) { _cut_low_y = cut_low_y; }
  void set_cut_high_x(int32_t cut_high_x) { _cut_high_x = cut_high_x; }
  void set_cut_high_y(int32_t cut_high_y) { _cut_high_y = cut_high_y; }
  void set_cut_area_um2(double cut_area_um2) { _cut_area_um2 = cut_area_um2; }
  void set_resistance(double resistance) { _resistance = resistance; }
  // function

 private:
  std::string _via_name;
  std::string _bottom_layer_name;
  std::string _top_layer_name;
  int32_t _bottom_layer_idx = -1;
  int32_t _top_layer_idx = -1;
  int32_t _x = 0;
  int32_t _y = 0;
  int32_t _bottom_x = 0;
  int32_t _bottom_y = 0;
  int32_t _top_x = 0;
  int32_t _top_y = 0;
  int32_t _cut_num = 0;
  int32_t _cut_low_x = 0;
  int32_t _cut_low_y = 0;
  int32_t _cut_high_x = 0;
  int32_t _cut_high_y = 0;
  double _cut_area_um2 = 0.0;
  double _resistance = 0.0;
};

}  // namespace iemir
