// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
#include "NetRoutingData.hpp"

namespace ilvs {

class DefRoutingData
{
 public:
  DefRoutingData() = default;
  ~DefRoutingData() = default;
  // getter
  std::map<std::string, NetRoutingData>& get_net_routing_data_map() { return _net_routing_data_map; }
  std::set<std::string>& get_power_net_name_set() { return _power_net_name_set; }
  std::set<std::string>& get_ground_net_name_set() { return _ground_net_name_set; }
  std::map<std::string, std::string>& get_power_instance_pin_net_map() { return _power_instance_pin_net_map; }
  std::map<std::string, std::string>& get_ground_instance_pin_net_map() { return _ground_instance_pin_net_map; }
  // const getter
  const std::map<std::string, NetRoutingData>& get_net_routing_data_map() const { return _net_routing_data_map; }
  const std::set<std::string>& get_power_net_name_set() const { return _power_net_name_set; }
  const std::set<std::string>& get_ground_net_name_set() const { return _ground_net_name_set; }
  const std::map<std::string, std::string>& get_power_instance_pin_net_map() const { return _power_instance_pin_net_map; }
  const std::map<std::string, std::string>& get_ground_instance_pin_net_map() const { return _ground_instance_pin_net_map; }
  // function
  void reset()
  {
    _net_routing_data_map.clear();
    _power_net_name_set.clear();
    _ground_net_name_set.clear();
    _power_instance_pin_net_map.clear();
    _ground_instance_pin_net_map.clear();
  }

 private:
  std::map<std::string, NetRoutingData> _net_routing_data_map;
  std::set<std::string> _power_net_name_set;
  std::set<std::string> _ground_net_name_set;
  std::map<std::string, std::string> _power_instance_pin_net_map;
  std::map<std::string, std::string> _ground_instance_pin_net_map;
};

}  // namespace ilvs
