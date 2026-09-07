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

#include "Direction.hpp"
#include "EXTLayerRect.hpp"
#include "RTHeader.hpp"
#include "Utility.hpp"

namespace irt {

class DRPatch
{
 public:
  DRPatch() = default;
  DRPatch(const PlanarRect& planar_rect, const int32_t layer_idx)
  {
    _patch.set_real_rect(planar_rect);
    _patch.set_layer_idx(layer_idx);
  }
  ~DRPatch() = default;
  // getter
  EXTLayerRect& get_patch() { return _patch; }
  double get_fixed_rect_cost() const { return _fixed_rect_cost; }
  double get_routed_rect_cost() const { return _routed_rect_cost; }
  Direction get_direction() const { return _direction; }
  int32_t get_overlap_area() const { return _overlap_area; }
  // const getter
  const EXTLayerRect& get_patch() const { return _patch; }
  // setter
  void set_patch(const EXTLayerRect& patch) { _patch = patch; }
  void set_fixed_rect_cost(const double fixed_rect_cost) { _fixed_rect_cost = fixed_rect_cost; }
  void set_routed_rect_cost(const double routed_rect_cost) { _routed_rect_cost = routed_rect_cost; }
  void set_direction(const Direction& direction) { _direction = direction; }
  void set_overlap_area(const int32_t overlap_area) { _overlap_area = overlap_area; }
  // function
  double getTotalCost() { return (_fixed_rect_cost + _routed_rect_cost); }

 private:
  EXTLayerRect _patch;
  double _fixed_rect_cost = 0.0;
  double _routed_rect_cost = 0.0;
  Direction _direction = Direction::kNone;
  int32_t _overlap_area = 0;
};

struct CmpDRPatch
{
  bool operator()(const DRPatch& first_patch, const DRPatch& second_patch, Direction& layer_direction) const
  {
    // fixed_rect_cost 大小升序
    if (first_patch.get_fixed_rect_cost() != second_patch.get_fixed_rect_cost()) {
      return first_patch.get_fixed_rect_cost() < second_patch.get_fixed_rect_cost();
    }
    // routed_rect_cost 大小升序
    if (first_patch.get_routed_rect_cost() != second_patch.get_routed_rect_cost()) {
      return first_patch.get_routed_rect_cost() < second_patch.get_routed_rect_cost();
    }
    // 层方向优先
    bool first_prefer = first_patch.get_direction() == layer_direction;
    bool second_prefer = second_patch.get_direction() == layer_direction;
    if (first_prefer != second_prefer) {
      return first_prefer;
    }
    // 重叠面积降序
    if (first_patch.get_overlap_area() != second_patch.get_overlap_area()) {
      return first_patch.get_overlap_area() > second_patch.get_overlap_area();
    }
    // real_rect比较
    const PlanarRect& first_real_rect = first_patch.get_patch().get_real_rect();
    const PlanarRect& second_real_rect = second_patch.get_patch().get_real_rect();
    if (first_real_rect != second_real_rect) {
      return CmpPlanarRectByXASC()(first_real_rect, second_real_rect);
    }
    // grid_rect比较
    return CmpPlanarRectByXASC()(first_patch.get_patch().get_grid_rect(), second_patch.get_patch().get_grid_rect());
  }
};

}  // namespace irt
