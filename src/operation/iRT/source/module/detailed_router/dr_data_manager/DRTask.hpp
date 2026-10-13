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

#include "ConnectType.hpp"
#include "DRGroup.hpp"
#include "LayerCoord.hpp"
#include "LayerRect.hpp"
#include "SortStatus.hpp"

namespace irt {

class DRTask
{
 public:
  DRTask() = default;
  DRTask(const DRTask&) = default;
  DRTask(DRTask&&) = default;
  DRTask& operator=(const DRTask&) = default;
  DRTask& operator=(DRTask&&) = default;
  ~DRTask() = default;
  // getter
  int32_t get_task_idx() const { return _task_idx; }
  int32_t get_net_idx() { return _net_idx; }
  int32_t get_component_idx() const { return _component_idx; }
  ConnectType& get_connect_type() { return _connect_type; }
  PlanarRect& get_bounding_box() { return _bounding_box; }
  // const getter
  const int32_t get_net_idx() const { return _net_idx; }
  const ConnectType& get_connect_type() const { return _connect_type; }
  const std::vector<DRGroup>& get_dr_group_list() const { return _dr_group_list; }
  const std::vector<LayerCoord>& get_sort_coord_list() const { return _sort_coord_list; }
  const PlanarRect& get_bounding_box() const { return _bounding_box; }
  // setter
  void set_task_idx(int32_t task_idx) { _task_idx = task_idx; }
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_component_idx(const int32_t component_idx) { _component_idx = component_idx; }
  void set_connect_type(const ConnectType& connect_type) { _connect_type = connect_type; }
  void set_dr_group_list(std::vector<DRGroup> dr_group_list)
  {
    _dr_group_list = std::move(dr_group_list);
    _sort_coord_list.clear();
    size_t coord_num = 0;
    for (const DRGroup& dr_group : _dr_group_list) {
      coord_num += dr_group.get_coord_direction_map().size();
    }
    _sort_coord_list.reserve(coord_num);
    for (const DRGroup& dr_group : _dr_group_list) {
      for (const auto& [coord, direction_set] : dr_group.get_coord_direction_map()) {
        _sort_coord_list.push_back(coord);
      }
    }
    std::ranges::sort(_sort_coord_list, CmpLayerCoordByLayerASC());
  }
  void set_bounding_box(const PlanarRect& bounding_box) { _bounding_box = bounding_box; }

 private:
  int32_t _task_idx = -1;
  int32_t _net_idx = -1;
  int32_t _component_idx = -1;
  ConnectType _connect_type = ConnectType::kNone;
  std::vector<DRGroup> _dr_group_list;
  std::vector<LayerCoord> _sort_coord_list;
  PlanarRect _bounding_box;
};

struct CmpDRTask
{
  bool operator()(const DRTask* first_task, const DRTask* second_task) const
  {
    // 时钟线网优先
    bool first_clock = first_task->get_connect_type() == ConnectType::kClock;
    bool second_clock = second_task->get_connect_type() == ConnectType::kClock;
    if (first_clock != second_clock) {
      return first_clock;
    }
    // BoundingBox 大小升序
    double first_routing_area = first_task->get_bounding_box().getArea();
    double second_routing_area = second_task->get_bounding_box().getArea();
    if (first_routing_area != second_routing_area) {
      return first_routing_area < second_routing_area;
    }
    // PinNum 降序
    int32_t first_pin_num = static_cast<int32_t>(first_task->get_dr_group_list().size());
    int32_t second_pin_num = static_cast<int32_t>(second_task->get_dr_group_list().size());
    if (first_pin_num != second_pin_num) {
      return first_pin_num > second_pin_num;
    }

    if (first_task->get_net_idx() != second_task->get_net_idx()) {
      return first_task->get_net_idx() < second_task->get_net_idx();
    }

    return std::ranges::lexicographical_compare(first_task->get_sort_coord_list(), second_task->get_sort_coord_list(), CmpLayerCoordByLayerASC());
  }
};

}  // namespace irt
