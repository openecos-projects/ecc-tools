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

#include "AccessPoint.hpp"
#include "Direction.hpp"
#include "EXTLayerRect.hpp"
#include "PlanarRect.hpp"
#include "PlanarCoord.hpp"
#include "RTHeader.hpp"

namespace irt {

enum class MacroPinEdge
{
  kNone = 0,
  kNorth = 1,
  kSouth = 2,
  kEast = 3,
  kWest = 4
};

class Pin
{
 public:
  Pin() = default;
  ~Pin() = default;
  // getter
  int32_t get_pin_idx() const { return _pin_idx; }
  std::string& get_pin_name() { return _pin_name; }
  std::string& get_inst_name() { return _inst_name; }
  std::string& get_cell_master_name() { return _cell_master_name; }
  int32_t get_orient() const { return _orient; }
  PlanarCoord& get_inst_origin() { return _inst_origin; }
  std::string& get_local_pin_name() { return _local_pin_name; }
  bool get_is_core() const { return _is_core; }
  bool get_is_macro() const { return _is_macro; }
  bool get_is_pad() const { return _is_pad; }
  PlanarRect& get_inst_bbox() { return _inst_bbox; }
  MacroPinEdge get_macro_pin_edge() const { return _macro_pin_edge; }
  Direction get_preferred_escape_direction() const { return _preferred_escape_direction; }
  int32_t get_preferred_conn_layer_idx() const { return _preferred_conn_layer_idx; }
  std::vector<EXTLayerRect>& get_routing_shape_list() { return _routing_shape_list; }
  std::vector<EXTLayerRect>& get_cut_shape_list() { return _cut_shape_list; }
  bool get_is_driven() const { return _is_driven; }
  AccessPoint& get_access_point() { return _access_point; }
  // setter
  void set_pin_idx(const int32_t pin_idx) { _pin_idx = pin_idx; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_inst_name(const std::string& inst_name) { _inst_name = inst_name; }
  void set_cell_master_name(const std::string& cell_master_name) { _cell_master_name = cell_master_name; }
  void set_orient(const int32_t orient) { _orient = orient; }
  void set_inst_origin(const PlanarCoord& inst_origin) { _inst_origin = inst_origin; }
  void set_local_pin_name(const std::string& local_pin_name) { _local_pin_name = local_pin_name; }
  void set_is_core(const bool is_core) { _is_core = is_core; }
  void set_is_macro(const bool is_macro) { _is_macro = is_macro; }
  void set_is_pad(const bool is_pad) { _is_pad = is_pad; }
  void set_inst_bbox(const PlanarRect& inst_bbox) { _inst_bbox = inst_bbox; }
  void set_macro_pin_edge(const MacroPinEdge macro_pin_edge) { _macro_pin_edge = macro_pin_edge; }
  void set_preferred_escape_direction(const Direction preferred_escape_direction) { _preferred_escape_direction = preferred_escape_direction; }
  void set_preferred_conn_layer_idx(const int32_t preferred_conn_layer_idx) { _preferred_conn_layer_idx = preferred_conn_layer_idx; }
  void set_routing_shape_list(const std::vector<EXTLayerRect>& routing_shape_list) { _routing_shape_list = routing_shape_list; }
  void set_cut_shape_list(const std::vector<EXTLayerRect>& cut_shape_list) { _cut_shape_list = cut_shape_list; }
  void set_is_driven(const bool is_driven) { _is_driven = is_driven; }
  void set_access_point(const AccessPoint& access_point) { _access_point = access_point; }
  // function

 private:
  int32_t _pin_idx = -1;
  std::string _pin_name;
  std::string _inst_name;
  std::string _cell_master_name;
  int32_t _orient = -1;
  PlanarCoord _inst_origin;
  std::string _local_pin_name;
  bool _is_core = false;
  bool _is_macro = false;
  bool _is_pad = false;
  PlanarRect _inst_bbox;
  MacroPinEdge _macro_pin_edge = MacroPinEdge::kNone;
  Direction _preferred_escape_direction = Direction::kNone;
  int32_t _preferred_conn_layer_idx = -1;
  std::vector<EXTLayerRect> _routing_shape_list;
  std::vector<EXTLayerRect> _cut_shape_list;
  bool _is_driven = false;
  AccessPoint _access_point;
};

}  // namespace irt
