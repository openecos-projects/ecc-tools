// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"
#include "PGSegmentType.hpp"

namespace ifp {

class PGSegment
{
 public:
  PGSegment() = default;
  ~PGSegment() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  std::string& get_layer_name() { return _layer_name; }
  PGSegmentType get_type() const { return _type; }
  int32_t get_width() const { return _width; }
  int32_t get_start_x() const { return _start_x; }
  int32_t get_start_y() const { return _start_y; }
  int32_t get_end_x() const { return _end_x; }
  int32_t get_end_y() const { return _end_y; }
  std::string& get_bottom_layer_name() { return _bottom_layer_name; }
  std::string& get_top_layer_name() { return _top_layer_name; }
  std::string& get_cut_layer_name() { return _cut_layer_name; }
  int32_t get_via_width() const { return _via_width; }
  int32_t get_via_height() const { return _via_height; }
  bool get_generated() const { return _generated; }

  // const getter
  const std::string& get_net_name() const { return _net_name; }
  const std::string& get_layer_name() const { return _layer_name; }
  const std::string& get_bottom_layer_name() const { return _bottom_layer_name; }
  const std::string& get_top_layer_name() const { return _top_layer_name; }
  const std::string& get_cut_layer_name() const { return _cut_layer_name; }

  // setter
  void set_net_name(std::string net_name) { _net_name = net_name; }
  void set_layer_name(std::string layer_name) { _layer_name = layer_name; }
  void set_type(PGSegmentType type) { _type = type; }
  void set_width(int32_t width) { _width = width; }
  void set_start_x(int32_t start_x) { _start_x = start_x; }
  void set_start_y(int32_t start_y) { _start_y = start_y; }
  void set_end_x(int32_t end_x) { _end_x = end_x; }
  void set_end_y(int32_t end_y) { _end_y = end_y; }
  void set_start_coord(int32_t start_x, int32_t start_y)
  {
    _start_x = start_x;
    _start_y = start_y;
  }
  void set_end_coord(int32_t end_x, int32_t end_y)
  {
    _end_x = end_x;
    _end_y = end_y;
  }
  void set_bottom_layer_name(std::string bottom_layer_name) { _bottom_layer_name = bottom_layer_name; }
  void set_top_layer_name(std::string top_layer_name) { _top_layer_name = top_layer_name; }
  void set_cut_layer_name(std::string cut_layer_name) { _cut_layer_name = cut_layer_name; }
  void set_via_width(int32_t via_width) { _via_width = via_width; }
  void set_via_height(int32_t via_height) { _via_height = via_height; }
  void set_generated(bool generated) { _generated = generated; }

  // function
  bool is_line() const { return _type == PGSegmentType::kFollowPin || _type == PGSegmentType::kStripe; }
  bool is_horizontal() const { return is_line() && _start_y == _end_y; }
  bool is_vertical() const { return is_line() && _start_x == _end_x; }
  int32_t get_ll_x() const { return std::min(_start_x, _end_x) - _width / 2; }
  int32_t get_ll_y() const { return std::min(_start_y, _end_y) - _width / 2; }
  int32_t get_ur_x() const { return std::max(_start_x, _end_x) + _width / 2; }
  int32_t get_ur_y() const { return std::max(_start_y, _end_y) + _width / 2; }

 private:
  std::string _net_name;
  std::string _layer_name;
  PGSegmentType _type = PGSegmentType::kNone;
  int32_t _width = -1;
  int32_t _start_x = -1;
  int32_t _start_y = -1;
  int32_t _end_x = -1;
  int32_t _end_y = -1;
  std::string _bottom_layer_name;
  std::string _top_layer_name;
  std::string _cut_layer_name;
  int32_t _via_width = -1;
  int32_t _via_height = -1;
  bool _generated = false;
};

}  // namespace ifp
