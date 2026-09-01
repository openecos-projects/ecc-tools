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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "MIDensityWindow.hpp"

namespace izh {

class MILayer
{
 public:
  MILayer() = default;
  ~MILayer() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  int32_t get_geometry_layer_idx() const { return _geometry_layer_idx; }
  bool get_is_horizontal() const { return _is_horizontal; }
  int32_t get_wire_width() const { return _wire_width; }
  int32_t get_min_wire_length() const { return _min_wire_length; }
  int32_t get_track_start() const { return _track_start; }
  int32_t get_track_pitch() const { return _track_pitch; }
  int32_t get_max_spacing() const { return _max_spacing; }
  int32_t get_density_window_x_num() const { return _density_window_x_num; }
  int32_t get_density_window_y_num() const { return _density_window_y_num; }
  std::vector<MIDensityWindow>& get_density_window_list() { return _density_window_list; }
  std::vector<MIRect>& get_fill_rect_list() { return _fill_rect_list; }
  int32_t get_inserted_metal_num() const { return _inserted_metal_num; }
  // const getter
  const std::string& get_layer_name() const { return _layer_name; }
  const std::vector<MIDensityWindow>& get_density_window_list() const { return _density_window_list; }
  const std::vector<MIRect>& get_fill_rect_list() const { return _fill_rect_list; }
  // setter
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_geometry_layer_idx(int32_t geometry_layer_idx) { _geometry_layer_idx = geometry_layer_idx; }
  void set_is_horizontal(bool is_horizontal) { _is_horizontal = is_horizontal; }
  void set_wire_width(int32_t wire_width) { _wire_width = wire_width; }
  void set_min_wire_length(int32_t min_wire_length) { _min_wire_length = min_wire_length; }
  void set_track_start(int32_t track_start) { _track_start = track_start; }
  void set_track_pitch(int32_t track_pitch) { _track_pitch = track_pitch; }
  void set_max_spacing(int32_t max_spacing) { _max_spacing = max_spacing; }
  void set_density_window_x_num(int32_t density_window_x_num) { _density_window_x_num = density_window_x_num; }
  void set_density_window_y_num(int32_t density_window_y_num) { _density_window_y_num = density_window_y_num; }
  void set_density_window_list(const std::vector<MIDensityWindow>& density_window_list) { _density_window_list = density_window_list; }
  void set_fill_rect_list(const std::vector<MIRect>& fill_rect_list) { _fill_rect_list = fill_rect_list; }
  void set_inserted_metal_num(int32_t inserted_metal_num) { _inserted_metal_num = inserted_metal_num; }
  // function
  int32_t get_density_window_idx(int32_t x_idx, int32_t y_idx) const { return y_idx * _density_window_x_num + x_idx; }
  void add_inserted_metal_num() { ++_inserted_metal_num; }

 private:
  std::string _layer_name;
  int32_t _geometry_layer_idx = -1;
  bool _is_horizontal = false;
  int32_t _wire_width = 0;
  int32_t _min_wire_length = 0;
  int32_t _track_start = 0;
  int32_t _track_pitch = 0;
  int32_t _max_spacing = 0;
  int32_t _density_window_x_num = 0;
  int32_t _density_window_y_num = 0;
  std::vector<MIDensityWindow> _density_window_list;
  std::vector<MIRect> _fill_rect_list;
  int32_t _inserted_metal_num = 0;
};

}  // namespace izh
