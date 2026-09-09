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

#include "EXTLayerRect.hpp"
#include "NetShape.hpp"
#include "Segment.hpp"
#include "Utility.hpp"

namespace irt {

// References stay valid until their owner net is removed or the box is released.
class DRShapeIndex
{
 public:
  struct Shape
  {
    int32_t net_idx = -1;
    Segment<LayerCoord>* segment = nullptr;
    EXTLayerRect* patch = nullptr;
    std::vector<LayerRect> rect_list;
  };

  void addSegment(int32_t net_idx, Segment<LayerCoord>* segment, const std::vector<NetShape>& net_shape_list)
  {
    Shape shape;
    shape.net_idx = net_idx;
    shape.segment = segment;
    shape.rect_list.reserve(net_shape_list.size());
    for (const NetShape& net_shape : net_shape_list) {
      shape.rect_list.push_back(net_shape);
    }
    addShape(std::move(shape));
  }
  void addPatch(int32_t net_idx, EXTLayerRect* patch)
  {
    Shape shape;
    shape.net_idx = net_idx;
    shape.patch = patch;
    shape.rect_list.push_back(patch->getRealLayerRect());
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
    _built = true;
  }
  void removeNet(int32_t net_idx)
  {
    auto net_iter = _net_shape_list_map.find(net_idx);
    if (net_iter == _net_shape_list_map.end()) {
      return;
    }
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
  void clear()
  {
    _layer_shape_rtree_map.clear();
    _net_shape_list_map.clear();
    _built = false;
  }

 private:
  using ShapeRTree = bgi::rtree<std::pair<BGRectInt, std::pair<int32_t, size_t>>, bgi::quadratic<16>>;

  bool _built = false;
  std::map<int32_t, ShapeRTree> _layer_shape_rtree_map;
  std::map<int32_t, std::vector<Shape>> _net_shape_list_map;

  void addShape(Shape shape)
  {
    std::vector<Shape>& shape_list = _net_shape_list_map[shape.net_idx];
    if (_built) {
      for (const LayerRect& rect : shape.rect_list) {
        _layer_shape_rtree_map[rect.get_layer_idx()].insert({Utility::convertToBGRectInt(rect), {shape.net_idx, shape_list.size()}});
      }
    }
    shape_list.push_back(std::move(shape));
  }
};

}  // namespace irt
