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
#include "DRBoxId.hpp"
#include "DRBoxResult.hpp"
#include "DRIterParam.hpp"
#include "DRNode.hpp"
#include "DRPatchState.hpp"
#include "DRRouteState.hpp"
#include "DRShadow.hpp"
#include "DRShapeIndex.hpp"
#include "DRTask.hpp"
#include "LayerCoord.hpp"
#include "LayerRect.hpp"
#include "ScaleAxis.hpp"
#include "Violation.hpp"

namespace irt {

struct DRFixedShape
{
  int32_t net_idx = -1;
  EXTLayerRect* rect = nullptr;
  bool is_routing = false;
};

class DRFixedGeometry
{
 public:
  using RectRTree = bgi::rtree<std::pair<BGRectInt, size_t>, bgi::quadratic<16>>;

  bool get_built() const { return _built; }
  const std::vector<DRFixedShape>& get_shape_list() const
  {
    if (!_built) {
      RTLOG.error(Loc::current(), "The fixed DR geometry has not been built!");
    }
    return _shape_list;
  }
  void build(const std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>>& fixed_rect_map);
  std::vector<size_t> query(const std::vector<LayerRect>& region_list) const;

 private:
  bool _built = false;
  std::vector<DRFixedShape> _shape_list;
  std::map<int32_t, RectRTree> _layer_rect_rtree_map;
};

class DRBox
{
 public:
  DRBox() = default;
  ~DRBox() = default;
  // getter
  DRBoxResult& get_curr_result() { return _curr_result; }
  DRBoxResult& get_best_result() { return _best_result; }
  EXTPlanarRect& get_box_rect() { return _box_rect; }
  DRBoxId& get_dr_box_id() { return _dr_box_id; }
  DRIterParam* get_dr_iter_param() { return _dr_iter_param; }
  bool get_initial_routing() const { return _initial_routing; }
  bool get_dirty() const { return _dirty; }
  bool get_refine_enabled() const { return _refine_enabled; }
  bool get_refine_routing() const { return _refine_routing; }
  std::vector<int32_t>& get_refine_net_list() { return _refine_net_list; }
  DRFixedGeometry& get_fixed_geometry() { return _fixed_geometry; }
  std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>>& get_net_access_point_map() { return _net_access_point_map; }
  std::map<int32_t, std::vector<Segment<LayerCoord>*>>& get_net_env_result_map() { return _net_env_result_map; }
  std::map<int32_t, std::vector<EXTLayerRect*>>& get_net_env_patch_map() { return _net_env_patch_map; }
  std::map<int32_t, std::map<int32_t, std::vector<Segment<LayerCoord>>>>& get_net_component_result_map() { return _net_component_result_map; }
  std::map<int32_t, int32_t>& get_net_routed_times_map() { return _net_routed_times_map; }
  std::vector<DRTask>& get_dr_task_list() { return _dr_task_list; }
  std::vector<int32_t>& get_task_order_list() { return _task_order_list; }
  ScaleAxis& get_box_track_axis() { return _box_track_axis; }
  std::vector<GridMap<DRNode>>& get_layer_node_map() { return _layer_node_map; }
  std::vector<DRShadow>& get_layer_shadow_map() { return _layer_shadow_map; }
  DRShapeIndex& get_env_shape_index() { return _env_shape_index; }
  DRShapeIndex& get_routed_shape_index() { return _routed_shape_index; }
  std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>>& get_layer_axis_map() { return _layer_axis_map; }

  // setter
  void set_box_rect(const EXTPlanarRect& box_rect) { _box_rect = box_rect; }
  void set_dr_box_id(const DRBoxId& dr_box_id) { _dr_box_id = dr_box_id; }
  void set_dr_iter_param(DRIterParam* dr_iter_param) { _dr_iter_param = dr_iter_param; }
  void set_initial_routing(const bool initial_routing) { _initial_routing = initial_routing; }
  void set_dirty(const bool dirty) { _dirty = dirty; }
  void set_refine_enabled(bool refine_enabled) { _refine_enabled = refine_enabled; }
  void set_refine_routing(bool refine_routing) { _refine_routing = refine_routing; }
  void set_net_access_point_map(const std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>>& net_access_point_map)
  {
    _net_access_point_map = net_access_point_map;
  }
  void set_net_env_result_map(const std::map<int32_t, std::vector<Segment<LayerCoord>*>>& net_env_result_map) { _net_env_result_map = net_env_result_map; }
  void set_net_env_patch_map(const std::map<int32_t, std::vector<EXTLayerRect*>>& net_env_patch_map) { _net_env_patch_map = net_env_patch_map; }
  void set_box_track_axis(const ScaleAxis& box_track_axis) { _box_track_axis = box_track_axis; }
  void set_layer_node_map(const std::vector<GridMap<DRNode>>& layer_node_map) { _layer_node_map = layer_node_map; }
  void set_layer_shadow_map(const std::vector<DRShadow>& layer_shadow_map) { _layer_shadow_map = layer_shadow_map; }
  void set_layer_axis_map(const std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>>& layer_axis_map) { _layer_axis_map = layer_axis_map; }
  // function
  DRRouteState& get_route_state() { return _route_state; }
  DRPatchState& get_patch_state() { return _patch_state; }

 private:
  DRBoxResult _curr_result;
  DRBoxResult _best_result;
  EXTPlanarRect _box_rect;
  DRBoxId _dr_box_id;
  DRIterParam* _dr_iter_param = nullptr;
  bool _initial_routing = true;
  bool _dirty = false;
  bool _refine_enabled = false;
  bool _refine_routing = false;
  std::vector<int32_t> _refine_net_list;
  // Environment references are borrowed from RTDM and inactive boxes for one schedule.
  DRFixedGeometry _fixed_geometry;
  std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>> _net_access_point_map;
  std::map<int32_t, std::vector<Segment<LayerCoord>*>> _net_env_result_map;
  std::map<int32_t, std::vector<EXTLayerRect*>> _net_env_patch_map;
  // Task, graph and best-result workspace is released by freeDRBox.
  std::map<int32_t, std::map<int32_t, std::vector<Segment<LayerCoord>>>> _net_component_result_map;
  std::map<int32_t, int32_t> _net_routed_times_map;
  std::vector<DRTask> _dr_task_list;
  std::vector<int32_t> _task_order_list;
  ScaleAxis _box_track_axis;
  std::vector<GridMap<DRNode>> _layer_node_map;
  std::vector<DRShadow> _layer_shadow_map;
  DRShapeIndex _env_shape_index;
  DRShapeIndex _routed_shape_index;
  std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>> _layer_axis_map;
  DRRouteState _route_state;
  DRPatchState _patch_state;
};

}  // namespace irt
