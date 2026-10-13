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

#include "RTHeader.hpp"

namespace irt {

class RegionRoute
{
 public:
  RegionRoute() = default;
  ~RegionRoute() = default;
  // getter
  bool get_enable() const { return _enable; }
  bool get_is_regional_stage() const { return _is_regional_stage; }
  std::set<int32_t>& get_net_idx_set() { return _net_idx_set; }
  // setter
  void set_enable(const bool enable) { _enable = enable; }
  void set_is_regional_stage(const bool is_regional_stage) { _is_regional_stage = is_regional_stage; }
  // function
  bool isActiveNet(int32_t net_idx) const { return !_enable || !_is_regional_stage || _net_idx_set.contains(net_idx); }

 private:
  bool _enable = false;
  bool _is_regional_stage = false;
  std::set<int32_t> _net_idx_set;
};

}  // namespace irt
