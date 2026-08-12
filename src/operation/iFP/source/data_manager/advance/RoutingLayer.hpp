// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Direction.hpp"
#include "FPHeader.hpp"

namespace ifp {

class RoutingLayer
{
 public:
  RoutingLayer() = default;
  ~RoutingLayer() = default;
  // getter
  std::string& get_name() { return _name; }
  int32_t get_layer_idx() const { return _layer_idx; }
  int32_t get_order() const { return _order; }
  int32_t get_pitch_x() const { return _pitch_x; }
  int32_t get_pitch_y() const { return _pitch_y; }
  int32_t get_prefer_track_pitch() const { return _prefer_track_pitch; }
  int32_t get_nonprefer_track_pitch() const { return _nonprefer_track_pitch; }
  int32_t get_prefer_track_offset() const { return _prefer_track_offset; }
  int32_t get_spacing() const { return _spacing; }
  Direction get_prefer_direction() const { return _prefer_direction; }

  // const getter
  const std::string& get_name() const { return _name; }

  // setter
  void set_name(std::string name) { _name = name; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_order(int32_t order) { _order = order; }
  void set_pitch_x(int32_t pitch_x) { _pitch_x = pitch_x; }
  void set_pitch_y(int32_t pitch_y) { _pitch_y = pitch_y; }
  void set_prefer_track_pitch(int32_t prefer_track_pitch) { _prefer_track_pitch = prefer_track_pitch; }
  void set_nonprefer_track_pitch(int32_t nonprefer_track_pitch) { _nonprefer_track_pitch = nonprefer_track_pitch; }
  void set_prefer_track_offset(int32_t prefer_track_offset) { _prefer_track_offset = prefer_track_offset; }
  void set_spacing(int32_t spacing) { _spacing = spacing; }
  void set_prefer_direction(Direction prefer_direction) { _prefer_direction = prefer_direction; }

  // function

 private:
  std::string _name;
  int32_t _layer_idx = -1;
  int32_t _order = -1;
  int32_t _pitch_x = -1;
  int32_t _pitch_y = -1;
  int32_t _prefer_track_pitch = -1;
  int32_t _nonprefer_track_pitch = -1;
  int32_t _prefer_track_offset = -1;
  int32_t _spacing = -1;
  Direction _prefer_direction = Direction::kNone;
};

}  // namespace ifp
