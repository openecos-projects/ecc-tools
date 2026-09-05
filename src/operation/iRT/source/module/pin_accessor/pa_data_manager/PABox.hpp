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
#include "LayerCoord.hpp"
#include "LayerRect.hpp"
#include "PABoxId.hpp"
#include "PABoxResult.hpp"
#include "PAIterParam.hpp"
#include "PANode.hpp"
#include "PAPatchState.hpp"
#include "PARouteState.hpp"
#include "PAShadow.hpp"
#include "PATask.hpp"
#include "ScaleAxis.hpp"
#include "Violation.hpp"

namespace irt {

class PABox
{
 public:
  PABox() = default;
  ~PABox() = default;
  // getter
  EXTPlanarRect& get_box_rect() { return _box_rect; }
  PABoxId& get_pa_box_id() { return _pa_box_id; }
  PAIterParam* get_pa_iter_param() { return _pa_iter_param; }
  bool get_initial_routing() const { return _initial_routing; }
  bool get_dirty() const { return _dirty; }
  std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>>& get_type_layer_net_fixed_rect_map() { return _type_layer_net_fixed_rect_map; }
  std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>>& get_net_access_point_map() { return _net_access_point_map; }
  std::map<int32_t, std::map<int32_t, std::set<Segment<LayerCoord>*>>>& get_net_pin_env_result_map() { return _net_pin_env_result_map; }
  std::map<int32_t, std::map<int32_t, std::vector<Segment<LayerCoord>>>>& get_net_pin_own_result_map() { return _net_pin_own_result_map; }
  std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>& get_net_pin_env_patch_map() { return _net_pin_env_patch_map; }
  std::map<int32_t, std::map<int32_t, std::vector<EXTLayerRect>>>& get_net_pin_own_patch_map() { return _net_pin_own_patch_map; }
  std::vector<PATask>& get_pa_task_list() { return _pa_task_list; }
  std::vector<int32_t>& get_task_order_list() { return _task_order_list; }
  ScaleAxis& get_box_track_axis() { return _box_track_axis; }
  std::vector<GridMap<PANode>>& get_layer_node_map() { return _layer_node_map; }
  std::vector<PAShadow>& get_layer_shadow_map() { return _layer_shadow_map; }
  std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>>& get_layer_axis_map() { return _layer_axis_map; }
  PABoxResult& get_curr_result() { return _curr_result; }
  PABoxResult& get_best_result() { return _best_result; }
  PARouteState& get_route_state() { return _route_state; }
  PAPatchState& get_patch_state() { return _patch_state; }
  // setter
  void set_box_rect(const EXTPlanarRect& box_rect) { _box_rect = box_rect; }
  void set_pa_box_id(const PABoxId& pa_box_id) { _pa_box_id = pa_box_id; }
  void set_pa_iter_param(PAIterParam* pa_iter_param) { _pa_iter_param = pa_iter_param; }
  void set_initial_routing(const bool initial_routing) { _initial_routing = initial_routing; }
  void set_dirty(const bool dirty) { _dirty = dirty; }
  void set_type_layer_net_fixed_rect_map(const std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>>& type_layer_net_fixed_rect_map)
  {
    _type_layer_net_fixed_rect_map = type_layer_net_fixed_rect_map;
  }
  void set_net_access_point_map(const std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>>& net_access_point_map)
  {
    _net_access_point_map = net_access_point_map;
  }
  void set_net_pin_env_result_map(const std::map<int32_t, std::map<int32_t, std::set<Segment<LayerCoord>*>>>& net_pin_env_result_map)
  {
    _net_pin_env_result_map = net_pin_env_result_map;
  }
  void set_net_pin_env_patch_map(const std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>& net_pin_env_patch_map)
  {
    _net_pin_env_patch_map = net_pin_env_patch_map;
  }
  void set_box_track_axis(const ScaleAxis& box_track_axis) { _box_track_axis = box_track_axis; }
  void set_layer_node_map(const std::vector<GridMap<PANode>>& layer_node_map) { _layer_node_map = layer_node_map; }
  void set_layer_shadow_map(const std::vector<PAShadow>& layer_shadow_map) { _layer_shadow_map = layer_shadow_map; }
  void set_layer_axis_map(const std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>>& layer_axis_map) { _layer_axis_map = layer_axis_map; }

 private:
  EXTPlanarRect _box_rect;
  PABoxId _pa_box_id;
  PAIterParam* _pa_iter_param = nullptr;
  bool _initial_routing = true;
  bool _dirty = false;
  std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>> _type_layer_net_fixed_rect_map;
  std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>> _net_access_point_map;
  std::map<int32_t, std::map<int32_t, std::set<Segment<LayerCoord>*>>> _net_pin_env_result_map;
  std::map<int32_t, std::map<int32_t, std::vector<Segment<LayerCoord>>>> _net_pin_own_result_map;
  std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>> _net_pin_env_patch_map;
  std::map<int32_t, std::map<int32_t, std::vector<EXTLayerRect>>> _net_pin_own_patch_map;
  // Indexed by task_idx; storage is frozen before routing publishes task pointers.
  std::vector<PATask> _pa_task_list;
  std::vector<int32_t> _task_order_list;
  ScaleAxis _box_track_axis;
  std::vector<GridMap<PANode>> _layer_node_map;
  std::vector<PAShadow> _layer_shadow_map;
  std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>> _layer_axis_map;

  PABoxResult _curr_result;
  PABoxResult _best_result;
  PARouteState _route_state;
  PAPatchState _patch_state;
};

}  // namespace irt
