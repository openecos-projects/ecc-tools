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
#include "SdcCommandUtils.hpp"

#include <algorithm>

namespace ista::sdc {

std::vector<std::string> resolveObjectList(Database& database, const std::vector<std::string>& object_list)
{
  std::vector<std::string> resolved_object_list;
  for (const std::string& object_name : object_list) {
    std::string resolved_object_name = object_name;
    if (!resolved_object_name.empty() && resolved_object_name.front() == '\\') {
      resolved_object_name.erase(resolved_object_name.begin());
    }
    if (resolved_object_name.rfind("[get_ports", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(10);
    }
    if (resolved_object_name.rfind("[get_pins", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(9);
      std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    }
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
      continue;
    }
    if (!resolved_object_name.empty() && resolved_object_name.back() == ']') {
      std::string trimmed_object_name = resolved_object_name;
      trimmed_object_name.pop_back();
      if (database.get_pin_map().count(trimmed_object_name) > 0) {
        resolved_object_list.push_back(trimmed_object_name);
        continue;
      }
    }
    std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
    }
  }
  return resolved_object_list;
}

TimingPortConstraint& getPortConstraint(Database& database, const std::string& port_name)
{
  TimingPortConstraint& port_constraint = database.get_timing_constraint().get_port_constraint_map()[port_name];
  port_constraint.set_port_name(port_name);
  return port_constraint;
}

}  // namespace ista::sdc
