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
#include "ConnectType.hpp"
#include "LayerCoord.hpp"
#include "LayerRect.hpp"
#include "PAGroup.hpp"
#include "PAPin.hpp"
#include "SortStatus.hpp"

namespace irt {

class PATask
{
 public:
  PATask() = default;
  PATask(const PATask&) = default;
  PATask(PATask&&) = default;
  PATask& operator=(const PATask&) = default;
  PATask& operator=(PATask&&) = default;
  ~PATask() = default;
  // getter
  int32_t get_net_idx() { return _net_idx; }
  int32_t get_task_idx() { return _task_idx; }
  PAPin* get_pa_pin() { return _pa_pin; }
  ConnectType& get_connect_type() { return _connect_type; }
  PlanarRect& get_bounding_box() { return _bounding_box; }
  int32_t get_routed_times() { return _routed_times; }
  AccessPoint* get_selected_access_point() { return _selected_access_point; }
  // const getter
  const int32_t get_net_idx() const { return _net_idx; }
  const ConnectType& get_connect_type() const { return _connect_type; }
  const std::vector<PAGroup>& get_pa_group_list() const { return _pa_group_list; }
  const std::vector<LayerCoord>& get_sort_coord_list() const { return _sort_coord_list; }
  const PlanarRect& get_bounding_box() const { return _bounding_box; }
  // setter
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_task_idx(const int32_t task_idx) { _task_idx = task_idx; }
  void set_pa_pin(PAPin* pa_pin) { _pa_pin = pa_pin; }
  void set_connect_type(const ConnectType& connect_type) { _connect_type = connect_type; }
  void set_pa_group_list(std::vector<PAGroup> pa_group_list)
  {
    _pa_group_list = std::move(pa_group_list);
    _sort_coord_list.clear();
    size_t coord_num = 0;
    for (const PAGroup& pa_group : _pa_group_list) {
      coord_num += pa_group.get_coord_list().size();
    }
    _sort_coord_list.reserve(coord_num);
    for (const PAGroup& pa_group : _pa_group_list) {
      const auto& coord_list = pa_group.get_coord_list();
      _sort_coord_list.insert(_sort_coord_list.end(), coord_list.begin(), coord_list.end());
    }
    std::sort(_sort_coord_list.begin(), _sort_coord_list.end(), CmpLayerCoordByLayerASC());
  }
  void set_bounding_box(const PlanarRect& bounding_box) { _bounding_box = bounding_box; }
  void set_routed_times(const int32_t routed_times) { _routed_times = routed_times; }
  void set_selected_access_point(AccessPoint* selected_access_point) { _selected_access_point = selected_access_point; }
  // function
  void addRoutedTimes() { ++_routed_times; }

 private:
  int32_t _net_idx = -1;
  int32_t _task_idx = -1;
  PAPin* _pa_pin = nullptr;
  ConnectType _connect_type = ConnectType::kNone;
  std::vector<PAGroup> _pa_group_list;
  std::vector<LayerCoord> _sort_coord_list;
  PlanarRect _bounding_box;
  int32_t _routed_times = 0;
  AccessPoint* _selected_access_point = nullptr;
};

struct CmpPATask
{
  bool operator()(const PATask* a, const PATask* b) const
  {
    SortStatus sort_status = SortStatus::kEqual;
    // 时钟线网优先
    if (sort_status == SortStatus::kEqual) {
      ConnectType a_connect_type = a->get_connect_type();
      ConnectType b_connect_type = b->get_connect_type();
      if (a_connect_type == ConnectType::kClock && b_connect_type != ConnectType::kClock) {
        sort_status = SortStatus::kTrue;
      } else if (a_connect_type != ConnectType::kClock && b_connect_type == ConnectType::kClock) {
        sort_status = SortStatus::kFalse;
      } else {
        sort_status = SortStatus::kEqual;
      }
    }
    // BoundingBox 大小升序
    if (sort_status == SortStatus::kEqual) {
      double a_routing_area = a->get_bounding_box().getArea();
      double b_routing_area = b->get_bounding_box().getArea();
      if (a_routing_area < b_routing_area) {
        sort_status = SortStatus::kTrue;
      } else if (a_routing_area == b_routing_area) {
        sort_status = SortStatus::kEqual;
      } else {
        sort_status = SortStatus::kFalse;
      }
    }
    // PinNum 降序
    if (sort_status == SortStatus::kEqual) {
      int32_t a_pin_num = static_cast<int32_t>(a->get_pa_group_list().size());
      int32_t b_pin_num = static_cast<int32_t>(b->get_pa_group_list().size());
      if (a_pin_num > b_pin_num) {
        sort_status = SortStatus::kTrue;
      } else if (a_pin_num == b_pin_num) {
        sort_status = SortStatus::kEqual;
      } else {
        sort_status = SortStatus::kFalse;
      }
    }

    if (sort_status == SortStatus::kEqual) {
      int32_t a_net_idx = a->get_net_idx();
      int32_t b_net_idx = b->get_net_idx();
      if (a_net_idx < b_net_idx) {
        sort_status = SortStatus::kTrue;
      } else if (a_net_idx == b_net_idx) {
        sort_status = SortStatus::kEqual;
      } else {
        sort_status = SortStatus::kFalse;
      }
    }

    if (sort_status == SortStatus::kEqual) {
      const std::vector<LayerCoord>& a_coord_list = a->get_sort_coord_list();
      const std::vector<LayerCoord>& b_coord_list = b->get_sort_coord_list();

      if (std::lexicographical_compare(a_coord_list.begin(), a_coord_list.end(), b_coord_list.begin(), b_coord_list.end(), CmpLayerCoordByLayerASC())) {
        sort_status = SortStatus::kTrue;
      } else if (a_coord_list == b_coord_list) {
        sort_status = SortStatus::kEqual;
      } else {
        sort_status = SortStatus::kFalse;
      }
    }

    if (sort_status == SortStatus::kTrue) {
      return true;
    } else if (sort_status == SortStatus::kFalse) {
      return false;
    }
    return false;
  }
};

}  // namespace irt
