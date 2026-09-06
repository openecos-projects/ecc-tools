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

#include "Logger.hpp"
#include "PlanarRect.hpp"
#include "Utility.hpp"

namespace irt {

class DRShadow
{
 public:
  using RectRTree = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>;
  using NetRectMap = std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>>;

  DRShadow() = default;
  ~DRShadow() = default;
  // getter
  const RectRTree& get_fixed_rect_rtree() const { return _fixed_rect_rtree; }
  const NetRectMap& get_net_routed_rect_map() const { return _net_routed_rect_map; }
  const std::set<PlanarRect, CmpPlanarRectByXASC>& get_violation_set() const { return _violation_set; }
  // function
  void addFixedRect(int32_t net_idx, const PlanarRect& rect)
  {
    if (_fixed_rect_built || rect.isIncorrect()) {
      RTLOG.error(Loc::current(), "Cannot collect an invalid DR shadow or modify its built fixed index!");
      return;
    }
    _fixed_rect_list.emplace_back(net_idx, rect);
  }
  void buildFixedRectRTree()
  {
    if (_fixed_rect_built) {
      RTLOG.error(Loc::current(), "The fixed DR shadow index has already been built!");
      return;
    }
    std::ranges::sort(_fixed_rect_list,
                      [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : CmpPlanarRectByXASC()(a.second, b.second); });
    _fixed_rect_list.erase(std::unique(_fixed_rect_list.begin(), _fixed_rect_list.end()), _fixed_rect_list.end());
    std::vector<RectRTree::value_type> value_list;
    value_list.reserve(_fixed_rect_list.size());
    for (const auto& [net_idx, rect] : _fixed_rect_list) {
      value_list.emplace_back(Utility::convertToBGRectInt(rect), net_idx);
    }
    _fixed_rect_rtree = RectRTree(value_list.begin(), value_list.end());
    std::vector<std::pair<int32_t, PlanarRect>>().swap(_fixed_rect_list);
    _fixed_rect_built = true;
  }
  void addRoutedRect(int32_t net_idx, const PlanarRect& rect)
  {
    if (rect.isIncorrect()) {
      RTLOG.error(Loc::current(), "Cannot insert an invalid routed DR shadow!");
      return;
    }
    if (_net_routed_rect_map[net_idx][rect]++ == 0) {
      _routed_rect_rtree.insert({Utility::convertToBGRectInt(rect), net_idx});
    }
  }
  void delRoutedRect(int32_t net_idx, const PlanarRect& rect)
  {
    auto net_iter = _net_routed_rect_map.find(net_idx);
    if (net_iter == _net_routed_rect_map.end()) {
      RTLOG.error(Loc::current(), "The DR shadow has no routed net to remove!");
      return;
    }
    auto rect_iter = net_iter->second.find(rect);
    if (rect_iter == net_iter->second.end()) {
      RTLOG.error(Loc::current(), "The DR shadow has no routed contribution to remove!");
      return;
    }
    if (--rect_iter->second == 0) {
      if (_routed_rect_rtree.remove({Utility::convertToBGRectInt(rect), net_idx}) != 1) {
        RTLOG.error(Loc::current(), "The routed DR shadow index disagrees with its contribution count!");
      }
      net_iter->second.erase(rect_iter);
    }
    if (net_iter->second.empty()) {
      _net_routed_rect_map.erase(net_iter);
    }
  }
  void addViolation(const PlanarRect& rect)
  {
    if (rect.isIncorrect()) {
      RTLOG.error(Loc::current(), "Cannot insert an invalid DR violation shadow!");
      return;
    }
    if (_violation_set.insert(rect).second) {
      _violation_rtree.insert({Utility::convertToBGRectInt(rect), -1});
    }
  }
  void clearViolation()
  {
    _violation_set.clear();
    _violation_rtree.clear();
  }
  double getFixedRectCost(int32_t net_idx, const PlanarRect& rect, double unit) const { return getRectCost(_fixed_rect_rtree, net_idx, rect, unit); }
  double getRoutedRectCost(int32_t net_idx, const PlanarRect& rect, double unit) const { return getRectCost(_routed_rect_rtree, net_idx, rect, unit); }
  double getViolationCost(const PlanarRect& rect, double unit) const
  {
    double cost = 0;
    for (auto iter = _violation_rtree.qbegin(bgi::intersects(Utility::convertToBGRectInt(rect))); iter != _violation_rtree.qend(); ++iter) {
      BGRectInt bg_rect = iter->first;
      if (Utility::isOpenOverlap(rect, Utility::convertToPlanarRect(bg_rect))) {
        cost += unit;
      }
    }
    return cost;
  }

 private:
  bool _fixed_rect_built = false;
  std::vector<std::pair<int32_t, PlanarRect>> _fixed_rect_list;
  RectRTree _fixed_rect_rtree;
  RectRTree _routed_rect_rtree;
  RectRTree _violation_rtree;
  NetRectMap _net_routed_rect_map;
  std::set<PlanarRect, CmpPlanarRectByXASC> _violation_set;

  double getRectCost(const RectRTree& rtree, int32_t net_idx, const PlanarRect& rect, double unit) const
  {
    double cost = 0;
    for (auto iter = rtree.qbegin(bgi::intersects(Utility::convertToBGRectInt(rect))); iter != rtree.qend(); ++iter) {
      if (iter->second == net_idx) {
        continue;
      }
      BGRectInt bg_rect = iter->first;
      if (Utility::isOpenOverlap(rect, Utility::convertToPlanarRect(bg_rect))) {
        cost += unit;
      }
    }
    return cost;
  }
};

}  // namespace irt
