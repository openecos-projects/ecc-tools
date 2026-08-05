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

#include "LayerCoord.hpp"
#include "LayerRect.hpp"

namespace irt {

class DRConnectivity
{
 public:
  int32_t addEntity(std::vector<LayerRect> shape_list)
  {
    _shape_list_list.push_back(std::move(shape_list));
    return static_cast<int32_t>(_shape_list_list.size()) - 1;
  }
  void build()
  {
    _parent_list.resize(_shape_list_list.size());
    for (size_t i = 0; i < _parent_list.size(); i++) {
      _parent_list[i] = static_cast<int32_t>(i);
    }
    std::map<int32_t, bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>> layer_rtree_map;
    for (size_t i = 0; i < _shape_list_list.size(); i++) {
      for (LayerRect& shape : _shape_list_list[i]) {
        BGRectInt rect(BGPointInt(shape.get_ll_x(), shape.get_ll_y()), BGPointInt(shape.get_ur_x(), shape.get_ur_y()));
        std::vector<std::pair<BGRectInt, int32_t>> overlap_list;
        layer_rtree_map[shape.get_layer_idx()].query(bgi::intersects(rect), std::back_inserter(overlap_list));
        for (auto& [overlap_rect, entity_idx] : overlap_list) {
          merge(static_cast<int32_t>(i), entity_idx);
        }
        layer_rtree_map[shape.get_layer_idx()].insert({rect, static_cast<int32_t>(i)});
      }
    }
  }
  void merge(int32_t first, int32_t second)
  {
    first = getRoot(first);
    second = getRoot(second);
    if (first != second) {
      _parent_list[second] = first;
    }
  }
  int32_t getRoot(int32_t entity_idx)
  {
    int32_t root = entity_idx;
    while (_parent_list[root] != root) {
      root = _parent_list[root];
    }
    while (_parent_list[entity_idx] != entity_idx) {
      int32_t parent = _parent_list[entity_idx];
      _parent_list[entity_idx] = root;
      entity_idx = parent;
    }
    return root;
  }
  std::set<int32_t> getRootSet(const LayerCoord& coord)
  {
    std::set<int32_t> root_set;
    for (size_t i = 0; i < _shape_list_list.size(); i++) {
      for (LayerRect& shape : _shape_list_list[i]) {
        if (coord.get_layer_idx() == shape.get_layer_idx() && RTUTIL.isInside(shape, coord.get_planar_coord())) {
          root_set.insert(getRoot(static_cast<int32_t>(i)));
          break;
        }
      }
    }
    return root_set;
  }
  std::vector<std::vector<LayerRect>>& get_shape_list_list() { return _shape_list_list; }

 private:
  std::vector<std::vector<LayerRect>> _shape_list_list;
  std::vector<int32_t> _parent_list;
};

}  // namespace irt
