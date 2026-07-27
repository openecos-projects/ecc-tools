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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
#include "NetRoutingGraph.hpp"
#include "Shape.hpp"
#include "SupplyRouteShape.hpp"

namespace ilvs {

class PhysicalGraph
{
 public:
  PhysicalGraph() = default;
  ~PhysicalGraph() = default;
  // getter
  std::map<int32_t, std::vector<std::string>>& get_component_net_name_map() { return _component_net_name_map; }
  std::map<int32_t, std::vector<Shape>>& get_component_shape_map() { return _component_shape_map; }
  std::map<std::string, int32_t>& get_terminal_component_map() { return _terminal_component_map; }
  std::map<std::string, NetRoutingGraph>& get_net_routing_graph_map() { return _net_routing_graph_map; }
  std::set<std::string>& get_power_net_name_set() { return _power_net_name_set; }
  std::set<std::string>& get_ground_net_name_set() { return _ground_net_name_set; }
  std::map<std::string, std::string>& get_power_instance_pin_net_map() { return _power_instance_pin_net_map; }
  std::map<std::string, std::string>& get_ground_instance_pin_net_map() { return _ground_instance_pin_net_map; }
  std::vector<SupplyRouteShape>& get_supply_route_shape_list() { return _supply_route_shape_list; }
  // const getter
  const std::map<int32_t, std::vector<std::string>>& get_component_net_name_map() const { return _component_net_name_map; }
  const std::map<int32_t, std::vector<Shape>>& get_component_shape_map() const { return _component_shape_map; }
  const std::map<std::string, int32_t>& get_terminal_component_map() const { return _terminal_component_map; }
  const std::map<std::string, NetRoutingGraph>& get_net_routing_graph_map() const { return _net_routing_graph_map; }
  const std::set<std::string>& get_power_net_name_set() const { return _power_net_name_set; }
  const std::set<std::string>& get_ground_net_name_set() const { return _ground_net_name_set; }
  const std::map<std::string, std::string>& get_power_instance_pin_net_map() const { return _power_instance_pin_net_map; }
  const std::map<std::string, std::string>& get_ground_instance_pin_net_map() const { return _ground_instance_pin_net_map; }
  const std::vector<SupplyRouteShape>& get_supply_route_shape_list() const { return _supply_route_shape_list; }
  // function
  void resetDerivedData()
  {
    _component_net_name_map.clear();
    _component_shape_map.clear();
    _terminal_component_map.clear();
    _supply_route_shape_list.clear();
  }
  void reset()
  {
    resetDerivedData();
    _net_routing_graph_map.clear();
    _power_net_name_set.clear();
    _ground_net_name_set.clear();
    _power_instance_pin_net_map.clear();
    _ground_instance_pin_net_map.clear();
  }

 private:
  std::map<int32_t, std::vector<std::string>> _component_net_name_map;
  std::map<int32_t, std::vector<Shape>> _component_shape_map;
  std::map<std::string, int32_t> _terminal_component_map;
  std::map<std::string, NetRoutingGraph> _net_routing_graph_map;
  std::set<std::string> _power_net_name_set;
  std::set<std::string> _ground_net_name_set;
  std::map<std::string, std::string> _power_instance_pin_net_map;
  std::map<std::string, std::string> _ground_instance_pin_net_map;
  std::vector<SupplyRouteShape> _supply_route_shape_list;
};

}  // namespace ilvs
