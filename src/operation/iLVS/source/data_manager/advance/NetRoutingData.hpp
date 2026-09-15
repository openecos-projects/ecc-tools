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
#include "RoutingShape.hpp"
#include "RoutingVia.hpp"

namespace ilvs {

class NetRoutingData
{
 public:
  NetRoutingData() = default;
  ~NetRoutingData() = default;
  // getter
  std::vector<RoutingShape>& get_wire_routing_shape_list() { return _wire_routing_shape_list; }
  std::vector<RoutingVia>& get_routing_via_list() { return _routing_via_list; }
  std::map<std::string, std::vector<RoutingShape>>& get_terminal_routing_shape_map() { return _terminal_routing_shape_map; }
  // const getter
  const std::string& get_driver_terminal_name() const { return _driver_terminal_name; }
  const std::vector<RoutingShape>& get_wire_routing_shape_list() const { return _wire_routing_shape_list; }
  const std::vector<RoutingVia>& get_routing_via_list() const { return _routing_via_list; }
  const std::map<std::string, std::vector<RoutingShape>>& get_terminal_routing_shape_map() const { return _terminal_routing_shape_map; }
  // setter
  void set_driver_terminal_name(const std::string& driver_terminal_name) { _driver_terminal_name = driver_terminal_name; }

 private:
  std::string _driver_terminal_name;
  std::vector<RoutingShape> _wire_routing_shape_list;
  std::vector<RoutingVia> _routing_via_list;
  std::map<std::string, std::vector<RoutingShape>> _terminal_routing_shape_map;
};

}  // namespace ilvs
