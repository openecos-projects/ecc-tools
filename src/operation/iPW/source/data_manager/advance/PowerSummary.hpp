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

#include "PowerGroupType.hpp"
#include "PowerValue.hpp"
#include "PWHeader.hpp"

namespace ipw {

class PowerSummary
{
 public:
  PowerSummary() = default;
  ~PowerSummary() = default;
  // getter
  std::map<PowerGroupType, PowerValue>& get_group_power_map() { return _group_power_map; }
  PowerValue& get_total_power_value() { return _total_power_value; }
  // function

 private:
  std::map<PowerGroupType, PowerValue> _group_power_map;
  PowerValue _total_power_value;
};

}  // namespace ipw
