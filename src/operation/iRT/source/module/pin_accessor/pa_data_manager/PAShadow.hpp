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

namespace irt {

class PAShadow
{
 public:
  PAShadow() = default;
  ~PAShadow() = default;
  // getter
  std::map<int32_t, std::set<PlanarRect, CmpPlanarRectByXASC>>& get_net_fixed_rect_map() { return _net_fixed_rect_map; }
  const std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>>& get_net_routed_rect_map() const { return _net_routed_rect_map; }
  std::set<PlanarRect, CmpPlanarRectByXASC>& get_violation_set() { return _violation_set; }
  // setter
  void set_net_fixed_rect_map(const std::map<int32_t, std::set<PlanarRect, CmpPlanarRectByXASC>>& net_fixed_rect_map)
  {
    _net_fixed_rect_map = net_fixed_rect_map;
  }
  void set_violation_set(const std::set<PlanarRect, CmpPlanarRectByXASC>& violation_set) { _violation_set = violation_set; }
  // function
  void addRoutedRect(int32_t net_idx, const PlanarRect& rect) { _net_routed_rect_map[net_idx][rect]++; }
  void delRoutedRect(int32_t net_idx, const PlanarRect& rect)
  {
    auto net_iter = _net_routed_rect_map.find(net_idx);
    if (net_iter == _net_routed_rect_map.end()) {
      RTLOG.error(Loc::current(), "The PA shadow has no routed net to remove!");
    }
    auto rect_iter = net_iter->second.find(rect);
    if (rect_iter == net_iter->second.end()) {
      RTLOG.error(Loc::current(), "The PA shadow has no routed contribution to remove!");
    }
    if (--rect_iter->second == 0) {
      net_iter->second.erase(rect_iter);
    }
    if (net_iter->second.empty()) {
      _net_routed_rect_map.erase(net_iter);
    }
  }

 private:
  std::map<int32_t, std::set<PlanarRect, CmpPlanarRectByXASC>> _net_fixed_rect_map;
  std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>> _net_routed_rect_map;
  std::set<PlanarRect, CmpPlanarRectByXASC> _violation_set;
};

}  // namespace irt
