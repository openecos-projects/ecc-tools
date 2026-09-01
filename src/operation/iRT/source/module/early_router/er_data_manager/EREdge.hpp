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

class EREdge
{
 public:
  EREdge() = default;
  ~EREdge() = default;
  // getter
  int32_t get_supply() const { return _supply; }
  int32_t get_demand() const { return _demand; }
  std::vector<int32_t>& get_demand_net_idx_list() { return _demand_net_idx_list; }
  std::set<int32_t>& get_ignore_net_set() { return _ignore_net_set; }
  int32_t get_overflow() const { return std::max(0, _demand - _supply); }
  // setter
  void set_supply(const int32_t supply) { _supply = supply; }
  void set_demand(const int32_t demand) { _demand = demand; }
  void set_ignore_net_set(const std::set<int32_t>& ignore_net_set) { _ignore_net_set = ignore_net_set; }

 private:
  int32_t _supply = 0;
  int32_t _demand = 0;
  std::vector<int32_t> _demand_net_idx_list;
  std::set<int32_t> _ignore_net_set;
};

}  // namespace irt
