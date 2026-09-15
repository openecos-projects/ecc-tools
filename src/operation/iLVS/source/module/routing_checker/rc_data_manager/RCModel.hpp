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
#include "RoutingCheck.hpp"

namespace ilvs {

class RCModel
{
 public:
  RCModel() = default;
  ~RCModel() = default;
  // getter
  std::vector<std::string>& get_net_name_list() { return _net_name_list; }
  std::vector<RoutingCheck>& get_routing_check_list() { return _routing_check_list; }
  std::vector<int32_t>& get_short_component_id_list() { return _short_component_id_list; }
  // const getter
  const std::vector<std::string>& get_net_name_list() const { return _net_name_list; }
  const std::vector<RoutingCheck>& get_routing_check_list() const { return _routing_check_list; }
  const std::vector<int32_t>& get_short_component_id_list() const { return _short_component_id_list; }
  // setter
  void set_net_name_list(const std::vector<std::string>& net_name_list) { _net_name_list = net_name_list; }
  void set_routing_check_list(const std::vector<RoutingCheck>& routing_check_list) { _routing_check_list = routing_check_list; }
  void set_short_component_id_list(const std::vector<int32_t>& short_component_id_list) { _short_component_id_list = short_component_id_list; }

 private:
  std::vector<std::string> _net_name_list;
  std::vector<RoutingCheck> _routing_check_list;
  std::vector<int32_t> _short_component_id_list;
};

}  // namespace ilvs
