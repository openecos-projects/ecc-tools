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
#include "DRCEngine.hpp"
#include "PinAccessor.hpp"

namespace irt {

bool PinAccessor::overlapCheckRegion(int32_t layer_idx, const PlanarRect& real_rect, const std::vector<LayerRect>& check_region_list)
{
  if (check_region_list.empty()) {
    return true;
  }
  for (const LayerRect& check_region : check_region_list) {
    if (layer_idx == check_region.get_layer_idx() && RTUTIL.isClosedOverlap(real_rect, check_region)) {
      return true;
    }
  }
  return false;
}

void PinAccessor::addFixedRectToDETask(DETask& de_task, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing)
{
  auto& shape_list = net_idx == -1 ? de_task.get_env_shape_list() : de_task.get_net_pin_shape_map()[net_idx];
  if (overlapCheckRegion(fixed_rect->get_layer_idx(), fixed_rect->get_real_rect(), de_task.get_check_region_list())) {
    shape_list.emplace_back(fixed_rect, is_routing);
  }
}

void PinAccessor::buildFixedDETask(DETask& de_task, const std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>>& fixed_rect_map)
{
  for (const auto& [is_routing, layer_net_fixed_rect_map] : fixed_rect_map) {
    for (const auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (const auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        if (net_idx != -1) {
          de_task.get_net_pin_shape_map().try_emplace(net_idx);
        }
        for (EXTLayerRect* fixed_rect : fixed_rect_set) {
          addFixedRectToDETask(de_task, net_idx, fixed_rect, is_routing);
        }
      }
    }
  }
}

void PinAccessor::addResultToDETask(DETask& de_task, int32_t net_idx, Segment<LayerCoord>* segment)
{
  std::vector<Segment<LayerCoord>*>& result_list = de_task.get_net_result_map()[net_idx];
  if (de_task.get_check_region_list().empty()) {
    result_list.push_back(segment);
    return;
  }
  for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
    if (overlapCheckRegion(net_shape.get_layer_idx(), net_shape.get_rect(), de_task.get_check_region_list())) {
      result_list.push_back(segment);
      return;
    }
  }
}

void PinAccessor::addPatchToDETask(DETask& de_task, int32_t net_idx, EXTLayerRect* patch)
{
  std::vector<EXTLayerRect*>& patch_list = de_task.get_net_patch_map()[net_idx];
  if (overlapCheckRegion(patch->get_layer_idx(), patch->get_real_rect(), de_task.get_check_region_list())) {
    patch_list.push_back(patch);
  }
}

void PinAccessor::buildCheckedNetSet(DETask& de_task)
{
  for (const auto& [net_idx, shape_list] : de_task.get_net_pin_shape_map()) {
    de_task.get_need_checked_net_set().insert(net_idx);
  }
  for (const auto& [net_idx, segment_list] : de_task.get_net_result_map()) {
    de_task.get_need_checked_net_set().insert(net_idx);
  }
  for (const auto& [net_idx, patch_list] : de_task.get_net_patch_map()) {
    de_task.get_need_checked_net_set().insert(net_idx);
  }
}

std::vector<Violation> PinAccessor::getPatchViolationList(PABox& pa_box, const std::set<ViolationType>& check_type_set,
                                                          const std::vector<LayerRect>& check_region_list)
{
  DETask de_task = buildPatchDETask(pa_box, check_type_set, check_region_list);
  return RTDE.getViolationList(de_task);
}

DETask PinAccessor::buildPatchDETask(PABox& pa_box, const std::set<ViolationType>& check_type_set, const std::vector<LayerRect>& check_region_list)
{
  DETask de_task;
  de_task.set_proc_type(DEProcType::kGet);
  de_task.set_net_type(DENetType::kPatchHybrid);
  de_task.set_top_name(RTUTIL.getString("pa_box_", pa_box.get_pa_box_id().get_x(), "_", pa_box.get_pa_box_id().get_y()));
  de_task.set_check_type_set(check_type_set);
  de_task.set_check_region_list(check_region_list);
  buildFixedDETask(de_task, pa_box.get_type_layer_net_fixed_rect_map());
  for (auto& [net_idx, pin_result_map] : pa_box.get_net_pin_env_result_map()) {
    for (auto& [pin_idx, segment_set] : pin_result_map) {
      de_task.get_net_result_map().try_emplace(net_idx);
      for (Segment<LayerCoord>* segment : segment_set) {
        addResultToDETask(de_task, net_idx, segment);
      }
    }
  }
  for (auto& [net_idx, task_result_map] : pa_box.get_curr_result().get_net_task_result_map()) {
    for (auto& [task_idx, segment_list] : task_result_map) {
      de_task.get_net_result_map().try_emplace(net_idx);
      for (Segment<LayerCoord>& segment : segment_list) {
        addResultToDETask(de_task, net_idx, &segment);
      }
    }
  }
  for (auto& [net_idx, pin_patch_map] : pa_box.get_net_pin_env_patch_map()) {
    for (auto& [pin_idx, patch_set] : pin_patch_map) {
      de_task.get_net_patch_map().try_emplace(net_idx);
      for (EXTLayerRect* patch : patch_set) {
        addPatchToDETask(de_task, net_idx, patch);
      }
    }
  }
  PATask* curr_task = pa_box.get_patch_state().get_curr_patch_task();
  for (auto& [net_idx, task_patch_map] : pa_box.get_curr_result().get_net_task_patch_map()) {
    for (auto& [task_idx, patch_list] : task_patch_map) {
      de_task.get_net_patch_map().try_emplace(net_idx);
      bool is_curr_task = net_idx == curr_task->get_net_idx() && task_idx == curr_task->get_task_idx();
      auto& checked_patch_list = is_curr_task ? pa_box.get_patch_state().get_routing_patch_list() : patch_list;
      for (EXTLayerRect& patch : checked_patch_list) {
        addPatchToDETask(de_task, net_idx, &patch);
      }
    }
  }
  for (PATask* pa_task : pa_box.get_pa_task_list()) {
    de_task.get_need_checked_net_set().insert(pa_task->get_net_idx());
  }
  buildCheckedNetSet(de_task);
  return de_task;
}

std::vector<Violation> PinAccessor::getRouteViolationList(DETask& route_task, DETask& ap_via_task)
{
  route_task.set_proc_type(DEProcType::kGet);
  route_task.set_net_type(DENetType::kRouteHybrid);
  std::vector<Violation> violation_list = RTDE.getViolationList(route_task);

  // Both views use the same fixed geometry, but AP-via-only must exclude all patches and other results.
  ap_via_task.set_proc_type(DEProcType::kGet);
  ap_via_task.set_net_type(DENetType::kRouteHybrid);
  ap_via_task.set_top_name(route_task.get_top_name());
  ap_via_task.set_env_shape_list(std::move(route_task.get_env_shape_list()));
  ap_via_task.set_net_pin_shape_map(std::move(route_task.get_net_pin_shape_map()));
  ap_via_task.set_check_type_set(route_task.get_check_type_set());
  ap_via_task.set_check_region_list(route_task.get_check_region_list());
  ap_via_task.set_skip_single_net_violation(true);
  std::vector<Violation> ap_violation_list = RTDE.getViolationList(ap_via_task);
  violation_list.insert(violation_list.end(), std::make_move_iterator(ap_violation_list.begin()), std::make_move_iterator(ap_violation_list.end()));
  return violation_list;
}

std::vector<Violation> PinAccessor::getRouteViolationList(PABox& pa_box)
{
  DETask route_task;
  DETask ap_via_task;
  route_task.set_top_name(RTUTIL.getString("pa_box_", pa_box.get_pa_box_id().get_x(), "_", pa_box.get_pa_box_id().get_y()));
  buildFixedDETask(route_task, pa_box.get_type_layer_net_fixed_rect_map());
  buildCheckedNetSet(route_task);
  for (PATask* pa_task : pa_box.get_pa_task_list()) {
    route_task.get_need_checked_net_set().insert(pa_task->get_net_idx());
  }
  ap_via_task.set_need_checked_net_set(route_task.get_need_checked_net_set());

  for (auto& [net_idx, pin_result_map] : pa_box.get_net_pin_env_result_map()) {
    for (auto& [pin_idx, segment_set] : pin_result_map) {
      auto& result_list = route_task.get_net_result_map()[net_idx];
      result_list.insert(result_list.end(), segment_set.begin(), segment_set.end());
    }
  }
  std::map<int32_t, std::map<int32_t, PATask*>> net_task_map;
  for (PATask* pa_task : pa_box.get_pa_task_list()) {
    net_task_map[pa_task->get_net_idx()][pa_task->get_task_idx()] = pa_task;
  }
  for (auto& [net_idx, task_result_map] : pa_box.get_curr_result().get_net_task_result_map()) {
    for (auto& [task_idx, segment_list] : task_result_map) {
      auto& result_list = route_task.get_net_result_map()[net_idx];
      for (Segment<LayerCoord>& segment : segment_list) {
        result_list.push_back(&segment);
      }
      if (RTUTIL.exist(net_task_map, net_idx) && RTUTIL.exist(net_task_map[net_idx], task_idx)) {
        LayerCoord access_coord = getAccessCoord(net_task_map[net_idx][task_idx]);
        auto& ap_result_list = ap_via_task.get_net_result_map()[net_idx];
        for (Segment<LayerCoord>& segment : segment_list) {
          if (isAPViaSegment(segment, access_coord)) {
            ap_result_list.push_back(&segment);
            break;
          }
        }
      }
    }
  }
  for (auto& [net_idx, pin_patch_map] : pa_box.get_net_pin_env_patch_map()) {
    for (auto& [pin_idx, patch_set] : pin_patch_map) {
      auto& patch_list = route_task.get_net_patch_map()[net_idx];
      patch_list.insert(patch_list.end(), patch_set.begin(), patch_set.end());
    }
  }
  for (auto& [net_idx, task_patch_map] : pa_box.get_curr_result().get_net_task_patch_map()) {
    for (auto& [task_idx, patch_list] : task_patch_map) {
      auto& result_patch_list = route_task.get_net_patch_map()[net_idx];
      for (EXTLayerRect& patch : patch_list) {
        result_patch_list.push_back(&patch);
      }
    }
  }
  buildCheckedNetSet(route_task);
  return getRouteViolationList(route_task, ap_via_task);
}

std::vector<Violation> PinAccessor::getFullRouteViolationList(PAModel& pa_model)
{
  DETask route_task;
  DETask ap_via_task;
  route_task.set_top_name("pa_model");
  auto& fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, fixed_rect_rtree] : fixed_rect_rtree_map[is_routing]) {
      for (const auto& [rect, net_fixed_rect] : fixed_rect_rtree) {
        addFixedRectToDETask(route_task, net_fixed_rect.first, net_fixed_rect.second, is_routing);
      }
    }
  }
  GridMap<PABox>& pa_box_map = pa_model.get_pa_box_map();
  for (int32_t x = 0; x < pa_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < pa_box_map.get_y_size(); y++) {
      PABox& pa_box = pa_box_map[x][y];
      for (auto& [net_idx, pin_result_map] : pa_box.get_net_pin_own_result_map()) {
        for (auto& [pin_idx, segment_list] : pin_result_map) {
          for (Segment<LayerCoord>& segment : segment_list) {
            route_task.get_net_result_map()[net_idx].push_back(&segment);
          }
          AccessPoint& access_point = pa_model.get_pa_net_list()[net_idx].get_pa_pin_list()[pin_idx].get_access_point();
          if (access_point.get_real_coord() == PlanarCoord(-1, -1) || access_point.get_layer_idx() < 0) {
            RTLOG.error(Loc::current(), "The pin access point is invalid!");
          }
          LayerCoord access_coord = access_point.getRealLayerCoord();
          for (Segment<LayerCoord>& segment : segment_list) {
            if (isAPViaSegment(segment, access_coord)) {
              ap_via_task.get_net_result_map()[net_idx].push_back(&segment);
              break;
            }
          }
        }
      }
      for (auto& [net_idx, pin_patch_map] : pa_box.get_net_pin_own_patch_map()) {
        for (auto& [pin_idx, patch_list] : pin_patch_map) {
          for (EXTLayerRect& patch : patch_list) {
            route_task.get_net_patch_map()[net_idx].push_back(&patch);
          }
        }
      }
    }
  }
  for (PANet& pa_net : pa_model.get_pa_net_list()) {
    route_task.get_need_checked_net_set().insert(pa_net.get_net_idx());
  }
  ap_via_task.set_need_checked_net_set(route_task.get_need_checked_net_set());
  return getRouteViolationList(route_task, ap_via_task);
}

std::vector<Violation> PinAccessor::getDirtyRouteViolationList(PAModel& pa_model, PABox& pa_box)
{
  DETask route_task;
  DETask ap_via_task;
  route_task.set_top_name(RTUTIL.getString("pa_box_", pa_box.get_pa_box_id().get_x(), "_", pa_box.get_pa_box_id().get_y(), "_dirty"));
  buildFixedDETask(route_task, RTDM.getTypeLayerNetFixedRectMap(pa_box.get_box_rect()));
  buildCheckedNetSet(route_task);
  for (auto& [net_idx, pin_result_map] : pa_box.get_net_pin_env_result_map()) {
    route_task.get_need_checked_net_set().insert(net_idx);
    for (auto& [pin_idx, segment_set] : pin_result_map) {
      for (Segment<LayerCoord>* segment : segment_set) {
        route_task.get_net_result_map()[net_idx].push_back(segment);
      }
      AccessPoint& access_point = pa_model.get_pa_net_list()[net_idx].get_pa_pin_list()[pin_idx].get_access_point();
      if (access_point.get_real_coord() == PlanarCoord(-1, -1) || access_point.get_layer_idx() < 0) {
        RTLOG.error(Loc::current(), "The environment pin access point is invalid!");
      }
      LayerCoord access_coord = access_point.getRealLayerCoord();
      for (Segment<LayerCoord>* segment : segment_set) {
        if (isAPViaSegment(*segment, access_coord)) {
          ap_via_task.get_net_result_map()[net_idx].push_back(segment);
          break;
        }
      }
    }
  }
  ap_via_task.set_need_checked_net_set(route_task.get_need_checked_net_set());
  for (auto& [net_idx, pin_patch_map] : pa_box.get_net_pin_env_patch_map()) {
    route_task.get_need_checked_net_set().insert(net_idx);
    for (auto& [pin_idx, patch_set] : pin_patch_map) {
      for (EXTLayerRect* patch : patch_set) {
        route_task.get_net_patch_map()[net_idx].push_back(patch);
      }
    }
  }
  for (RoutingLayer& routing_layer : RTDM.getDatabase().get_routing_layer_list()) {
    route_task.get_check_region_list().emplace_back(pa_box.get_box_rect().get_real_rect(), routing_layer.get_layer_idx());
  }
  std::vector<Violation> owned_violation_list;
  for (Violation& violation : getRouteViolationList(route_task, ap_via_task)) {
    PABoxId owner_box_id = getPABoxId(pa_model, violation.get_violation_shape().get_real_rect().getMidPoint());
    if (owner_box_id.get_x() == pa_box.get_pa_box_id().get_x() && owner_box_id.get_y() == pa_box.get_pa_box_id().get_y()) {
      owned_violation_list.push_back(std::move(violation));
    }
  }
  return owned_violation_list;
}

}  // namespace irt
