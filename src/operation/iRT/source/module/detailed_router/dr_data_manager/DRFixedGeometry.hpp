// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under the Mulan PSL v2.
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

#include "EXTLayerRect.hpp"
#include "LayerRect.hpp"
#include "Logger.hpp"
#include "Utility.hpp"

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
  using FixedRectMap = std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>>;

  bool get_built() const { return _built; }
  const std::vector<DRFixedShape>& get_shape_list() const
  {
    if (!_built) {
      RTLOG.error(Loc::current(), "The fixed DR geometry has not been built!");
    }
    return _shape_list;
  }
  void build(const FixedRectMap& fixed_rect_map)
  {
    if (_built) {
      RTLOG.error(Loc::current(), "The fixed DR geometry has already been built!");
    }
    size_t shape_num = 0;
    for (const auto& [is_routing, layer_net_rect_map] : fixed_rect_map) {
      for (const auto& [layer_idx, net_rect_map] : layer_net_rect_map) {
        for (const auto& [net_idx, rect_set] : net_rect_map) {
          shape_num += rect_set.size();
        }
      }
    }
    _shape_list.reserve(shape_num);
    std::map<int32_t, std::vector<RectRTree::value_type>> layer_value_map;
    for (const auto& [is_routing, layer_net_rect_map] : fixed_rect_map) {
      for (const auto& [layer_idx, net_rect_map] : layer_net_rect_map) {
        auto& value_list = layer_value_map[layer_idx];
        for (const auto& [net_idx, rect_set] : net_rect_map) {
          for (EXTLayerRect* rect : rect_set) {
            if (rect == nullptr || rect->get_layer_idx() != layer_idx || rect->get_real_rect().isIncorrect()) {
              RTLOG.error(Loc::current(), "Invalid fixed DR geometry on layer ", layer_idx);
            }
            value_list.emplace_back(Utility::convertToBGRectInt(rect->get_real_rect()), _shape_list.size());
            _shape_list.push_back({net_idx, rect, is_routing});
          }
        }
      }
    }
    for (const auto& [layer_idx, value_list] : layer_value_map) {
      _layer_rect_rtree_map.emplace(layer_idx, RectRTree(value_list.begin(), value_list.end()));
    }
    _built = true;
  }
  std::vector<size_t> query(const std::vector<LayerRect>& region_list) const
  {
    if (!_built) {
      RTLOG.error(Loc::current(), "The fixed DR geometry has not been built!");
    }
    std::vector<size_t> shape_idx_list;
    if (region_list.empty()) {
      shape_idx_list.resize(_shape_list.size());
      std::iota(shape_idx_list.begin(), shape_idx_list.end(), size_t{0});
      return shape_idx_list;
    }
    for (const LayerRect& region : region_list) {
      if (region.isIncorrect()) {
        RTLOG.error(Loc::current(), "Invalid fixed DR geometry query region!");
      }
      auto layer_iter = _layer_rect_rtree_map.find(region.get_layer_idx());
      if (layer_iter == _layer_rect_rtree_map.end()) {
        continue;
      }
      const RectRTree& rtree = layer_iter->second;
      for (auto shape_iter = rtree.qbegin(bgi::intersects(Utility::convertToBGRectInt(region))); shape_iter != rtree.qend(); ++shape_iter) {
        shape_idx_list.push_back(shape_iter->second);
      }
    }
    std::ranges::sort(shape_idx_list);
    shape_idx_list.erase(std::unique(shape_idx_list.begin(), shape_idx_list.end()), shape_idx_list.end());
    return shape_idx_list;
  }

 private:
  bool _built = false;
  std::vector<DRFixedShape> _shape_list;
  std::map<int32_t, RectRTree> _layer_rect_rtree_map;
};

}  // namespace irt
