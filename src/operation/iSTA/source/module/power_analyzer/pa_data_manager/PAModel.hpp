// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PowerGroupType.hpp"
#include "PowerValue.hpp"
#include "STAHeader.hpp"

namespace ista {

class PAModel
{
 public:
  PAModel() = default;
  ~PAModel() = default;
  // getter
  std::vector<std::string>& get_instance_name_list() { return _instance_name_list; }
  std::map<PowerGroupType, PowerValue>& get_group_power_map() { return _group_power_map; }
  // setter
  void set_instance_name_list(const std::vector<std::string>& instance_name_list) { _instance_name_list = instance_name_list; }
  void set_group_power_map(const std::map<PowerGroupType, PowerValue>& group_power_map) { _group_power_map = group_power_map; }
  // function

 private:
  std::vector<std::string> _instance_name_list;
  std::map<PowerGroupType, PowerValue> _group_power_map;
};

}  // namespace ista
