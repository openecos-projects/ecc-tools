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

#include <array>

#include "DataManager.hpp"
#include "EXTLayerRect.hpp"
#include "NetShape.hpp"
#include "RoutingLayer.hpp"
#include "Segment.hpp"
#include "Utility.hpp"

namespace irt {

class DRShapeIndex
{
  using ShapeRTree = bgi::rtree<std::pair<BGRectInt, std::pair<int32_t, size_t>>, bgi::quadratic<16>>;
  using HaloRTree = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>;

public:
  struct Shape
  {
    int32_t net_idx = -1;
    Segment<LayerCoord>* segment = nullptr;
    EXTLayerRect* patch = nullptr;
    std::vector<LayerRect> rect_list;
    std::vector<LayerRect> halo_rect_list;
  };

  void addFixedRect(int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing)
  {
    NetShape net_shape(net_idx, fixed_rect->getRealLayerRect(), is_routing);
    if (!net_shape.get_is_routing()) {
      return;
    }
    if (_built) {
      RTLOG.error(Loc::current(), "Cannot collect a fixed DR halo rect after the shape index has been built!");
      return;
    }
    for (const PlanarRect& halo_rect : getRoutingShadowShapeList(net_shape)) {
      _net_halo_rect_set_map[net_idx].emplace(halo_rect, net_shape.get_layer_idx());
    }
  }

  void addSegment(int32_t net_idx, Segment<LayerCoord>* segment, const std::vector<NetShape>& net_shape_list)
  {
    Shape shape;
    shape.net_idx = net_idx;
    shape.segment = segment;
    shape.rect_list.reserve(net_shape_list.size());
    for (const NetShape& net_shape : net_shape_list) {
      shape.rect_list.push_back(net_shape);
      if (net_shape.get_is_routing()) {
        for (const PlanarRect& halo_rect : getRoutingShadowShapeList(net_shape)) {
          shape.halo_rect_list.emplace_back(halo_rect, net_shape.get_layer_idx());
        }
      }
    }
    addShape(std::move(shape));
  }

  void addPatch(int32_t net_idx, EXTLayerRect* patch)
  {
    Shape shape;
    shape.net_idx = net_idx;
    shape.patch = patch;
    shape.rect_list.push_back(patch->getRealLayerRect());
    NetShape net_shape(net_idx, patch->getRealLayerRect(), true);
    for (const PlanarRect& halo_rect : getRoutingShadowShapeList(net_shape)) {
      shape.halo_rect_list.emplace_back(halo_rect, net_shape.get_layer_idx());
    }
    addShape(std::move(shape));
  }

  void build()
  {
    if (_built) {
      RTLOG.error(Loc::current(), "The DR shape index has already been built!");
    }
    std::map<int32_t, std::vector<ShapeRTree::value_type>> layer_value_list_map;
    for (auto& [net_idx, shape_list] : _net_shape_list_map) {
      for (size_t i = 0; i < shape_list.size(); i++) {
        for (const LayerRect& rect : shape_list[i].rect_list) {
          layer_value_list_map[rect.get_layer_idx()].emplace_back(Utility::convertToBGRectInt(rect), std::make_pair(net_idx, i));
        }
      }
    }
    for (auto& [layer_idx, value_list] : layer_value_list_map) {
      _layer_shape_rtree_map[layer_idx] = ShapeRTree(value_list.begin(), value_list.end());
    }
    std::map<int32_t, std::vector<HaloRTree::value_type>> layer_halo_value_map;
    for (auto& [net_idx, halo_rect_set] : _net_halo_rect_set_map) {
      for (const LayerRect& halo_rect : halo_rect_set) {
        layer_halo_value_map[halo_rect.get_layer_idx()].emplace_back(Utility::convertToBGRectInt(halo_rect), net_idx);
      }
    }
    for (auto& [layer_idx, value_list] : layer_halo_value_map) {
      _layer_halo_rtree_map[layer_idx] = HaloRTree(value_list.begin(), value_list.end());
    }
    _built = true;
  }

  void removeNet(int32_t net_idx)
  {
    auto net_iter = _net_shape_list_map.find(net_idx);
    if (net_iter != _net_shape_list_map.end()) {
      if (_built) {
        for (size_t i = 0; i < net_iter->second.size(); i++) {
          for (const LayerRect& rect : net_iter->second[i].rect_list) {
            if (_layer_shape_rtree_map.at(rect.get_layer_idx()).remove({Utility::convertToBGRectInt(rect), {net_idx, i}}) != 1) {
              RTLOG.error(Loc::current(), "The DR shape index disagrees with its net geometry!");
            }
          }
        }
      }
      _net_shape_list_map.erase(net_iter);
    }
    auto halo_iter = _net_halo_rect_set_map.find(net_idx);
    if (halo_iter != _net_halo_rect_set_map.end()) {
      if (_built) {
        for (const LayerRect& halo_rect : halo_iter->second) {
          if (_layer_halo_rtree_map.at(halo_rect.get_layer_idx()).remove({Utility::convertToBGRectInt(halo_rect), net_idx}) != 1) {
            RTLOG.error(Loc::current(), "The DR halo index disagrees with its net geometry!");
          }
        }
      }
      _net_halo_rect_set_map.erase(halo_iter);
    }
  }

  std::vector<const Shape*> query(const std::vector<LayerRect>& check_region_list) const
  {
    if (!_built) {
      RTLOG.error(Loc::current(), "The DR shape index has not been built!");
    }
    std::vector<const Shape*> shape_list;
    if (check_region_list.empty()) {
      for (const auto& [net_idx, net_shape_list] : _net_shape_list_map) {
        for (const Shape& shape : net_shape_list) {
          shape_list.push_back(&shape);
        }
      }
      return shape_list;
    }
    std::vector<std::pair<int32_t, size_t>> shape_id_list;
    for (const LayerRect& check_region : check_region_list) {
      auto layer_iter = _layer_shape_rtree_map.find(check_region.get_layer_idx());
      if (layer_iter == _layer_shape_rtree_map.end()) {
        continue;
      }
      const ShapeRTree& rtree = layer_iter->second;
      for (auto iter = rtree.qbegin(bgi::intersects(Utility::convertToBGRectInt(check_region))); iter != rtree.qend(); ++iter) {
        shape_id_list.push_back(iter->second);
      }
    }
    // A via or overlapping check regions can hit an object more than once. Preserve input order within each net.
    std::ranges::sort(shape_id_list);
    shape_id_list.erase(std::unique(shape_id_list.begin(), shape_id_list.end()), shape_id_list.end());
    shape_list.reserve(shape_id_list.size());
    for (const auto& [net_idx, shape_idx] : shape_id_list) {
      shape_list.push_back(&_net_shape_list_map.at(net_idx)[shape_idx]);
    }
    return shape_list;
  }

  double getOverlapCost(int32_t net_idx, int32_t layer_idx, const PlanarRect& rect, double unit) const
  {
    if (!_built) {
      RTLOG.error(Loc::current(), "The DR shape index has not been built!");
    }
    auto layer_iter = _layer_halo_rtree_map.find(layer_idx);
    if (layer_iter == _layer_halo_rtree_map.end()) {
      return 0;
    }
    double cost = 0;
    const HaloRTree& halo_rtree = layer_iter->second;
    for (auto iter = halo_rtree.qbegin(bgi::intersects(Utility::convertToBGRectInt(rect))); iter != halo_rtree.qend(); ++iter) {
      if (iter->second == net_idx) {
        continue;
      }
      BGRectInt halo_rect = iter->first;
      if (Utility::isOpenOverlap(rect, Utility::convertToPlanarRect(halo_rect))) {
        cost += unit;
      }
    }
    return cost;
  }

  void clear()
  {
    _layer_shape_rtree_map.clear();
    _layer_halo_rtree_map.clear();
    _net_shape_list_map.clear();
    _net_halo_rect_set_map.clear();
    _built = false;
  }
  // debug
  const std::map<int32_t, HaloRTree>& get_layer_halo_rtree_map() const { return _layer_halo_rtree_map; }

private:
  bool _built = false;
  std::map<int32_t, ShapeRTree> _layer_shape_rtree_map;
  std::map<int32_t, HaloRTree> _layer_halo_rtree_map;
  std::map<int32_t, std::vector<Shape>> _net_shape_list_map;
  std::map<int32_t, std::set<LayerRect, CmpLayerRectByXASC>> _net_halo_rect_set_map;

  void addShape(Shape shape)
  {
    std::vector<Shape>& shape_list = _net_shape_list_map[shape.net_idx];
    if (_built) {
      for (const LayerRect& rect : shape.rect_list) {
        _layer_shape_rtree_map[rect.get_layer_idx()].insert({Utility::convertToBGRectInt(rect), {shape.net_idx, shape_list.size()}});
      }
      for (const LayerRect& halo_rect : shape.halo_rect_list) {
        if (_net_halo_rect_set_map[shape.net_idx].insert(halo_rect).second) {
          _layer_halo_rtree_map[halo_rect.get_layer_idx()].insert({Utility::convertToBGRectInt(halo_rect), shape.net_idx});
        }
      }
    } else {
      for (const LayerRect& halo_rect : shape.halo_rect_list) {
        _net_halo_rect_set_map[shape.net_idx].insert(halo_rect);
      }
    }
    shape_list.push_back(std::move(shape));
  }

  std::vector<PlanarRect> getRoutingShadowShapeList(const NetShape& net_shape)
  {
    if (!net_shape.get_is_routing()) {
      RTLOG.error(Loc::current(), "The type of net_shape is cut!");
      return {};
    }
    std::array<std::pair<int32_t, int32_t>, 2> spacing_pair_list = getRoutingSpacingPairList(net_shape);
    std::vector<PlanarRect> shadow_shape_list;
    shadow_shape_list.reserve(spacing_pair_list.size());
    for (auto& [x_spacing, y_spacing] : spacing_pair_list) {
      // 膨胀size为 spacing
      int32_t enlarged_x_size = x_spacing;
      int32_t enlarged_y_size = y_spacing;
      // 贴合的也不算违例
      enlarged_x_size = std::max(enlarged_x_size - 1, 0);
      enlarged_y_size = std::max(enlarged_y_size - 1, 0);
      shadow_shape_list.push_back(RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_x_size, enlarged_y_size, enlarged_x_size, enlarged_y_size));
    }
    return shadow_shape_list;
  }

  std::array<std::pair<int32_t, int32_t>, 2> getRoutingSpacingPairList(const NetShape& net_shape)
  {
    RoutingLayer& routing_layer = RTDM.getDatabase().get_routing_layer_list()[net_shape.get_layer_idx()];
    int32_t prl_spacing = routing_layer.getPRLSpacing(net_shape.get_rect());
    int32_t max_eol_spacing = std::max(routing_layer.get_eol_spacing(), routing_layer.get_eol_ete());
    std::pair<int32_t, int32_t> eol_spacing = {max_eol_spacing, routing_layer.get_eol_within()};
    if (!routing_layer.isPreferH()) {
      std::swap(eol_spacing.first, eol_spacing.second);
    }
    return {std::make_pair(prl_spacing, prl_spacing), eol_spacing};
  }
};

}  // namespace irt
