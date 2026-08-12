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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"
#include "InstancePinShape.hpp"
#include "PlacementOrientation.hpp"
#include "PlanarRect.hpp"

namespace ifp {

class Instance
{
 public:
  Instance() = default;
  ~Instance() = default;
  // getter
  std::string& get_name() { return _name; }
  std::string& get_master_name() { return _master_name; }
  PlacementOrientation get_orient() const { return _orient; }
  int32_t get_x() const { return _x; }
  int32_t get_y() const { return _y; }
  int32_t get_width() const { return _width; }
  int32_t get_height() const { return _height; }
  PlanarRect& get_bounding_rect() { return _bounding_rect; }
  PlanarRect& get_placement_halo_rect() { return _placement_halo_rect; }
  PlanarRect& get_routing_halo_rect() { return _routing_halo_rect; }
  std::vector<InstancePinShape>& get_pin_shape_list() { return _pin_shape_list; }
  bool get_macro() const { return _macro; }
  bool get_fixed() const { return _fixed; }
  bool get_cover() const { return _cover; }
  bool get_placed() const { return _placed; }
  bool is_new_instance() const { return _new_instance; }
  bool is_placement_updated() const { return _placement_updated; }

  // const getter
  const std::string& get_name() const { return _name; }
  const std::string& get_master_name() const { return _master_name; }
  const PlanarRect& get_bounding_rect() const { return _bounding_rect; }
  const PlanarRect& get_placement_halo_rect() const { return _placement_halo_rect; }
  const PlanarRect& get_routing_halo_rect() const { return _routing_halo_rect; }
  const std::vector<InstancePinShape>& get_pin_shape_list() const { return _pin_shape_list; }

  // setter
  void set_name(std::string name) { _name = name; }
  void set_master_name(std::string master_name) { _master_name = master_name; }
  void set_orient(PlacementOrientation orient) { _orient = orient; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_coord(int32_t x, int32_t y)
  {
    _x = x;
    _y = y;
  }
  void set_width(int32_t width) { _width = width; }
  void set_height(int32_t height) { _height = height; }
  void set_bounding_rect(const PlanarRect& bounding_rect) { _bounding_rect = bounding_rect; }
  void set_bounding_rect(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y) { _bounding_rect.set_rect(ll_x, ll_y, ur_x, ur_y); }
  void set_placement_halo_rect(const PlanarRect& placement_halo_rect) { _placement_halo_rect = placement_halo_rect; }
  void set_routing_halo_rect(const PlanarRect& routing_halo_rect) { _routing_halo_rect = routing_halo_rect; }
  void set_pin_shape_list(const std::vector<InstancePinShape>& pin_shape_list) { _pin_shape_list = pin_shape_list; }
  void set_macro(bool macro) { _macro = macro; }
  void set_fixed(bool fixed) { _fixed = fixed; }
  void set_cover(bool cover) { _cover = cover; }
  void set_placed(bool placed) { _placed = placed; }
  void set_new_instance(bool new_instance) { _new_instance = new_instance; }
  void set_placement_updated(bool placement_updated) { _placement_updated = placement_updated; }

  // function

 private:
  std::string _name;
  std::string _master_name;
  PlacementOrientation _orient = PlacementOrientation::kNone;
  int32_t _x = -1;
  int32_t _y = -1;
  int32_t _width = -1;
  int32_t _height = -1;
  PlanarRect _bounding_rect;
  PlanarRect _placement_halo_rect;
  PlanarRect _routing_halo_rect;
  std::vector<InstancePinShape> _pin_shape_list;
  bool _macro = false;
  bool _fixed = false;
  bool _cover = false;
  bool _placed = false;
  bool _new_instance = false;
  bool _placement_updated = false;
};

}  // namespace ifp
