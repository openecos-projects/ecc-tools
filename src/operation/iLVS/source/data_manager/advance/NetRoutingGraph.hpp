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
#include "RoutingShape.hpp"

namespace ilvs {

class NetRoutingGraph
{
 public:
  NetRoutingGraph() = default;
  ~NetRoutingGraph() = default;
  // getter
  std::string& get_driver_terminal_name() { return _driver_terminal_name; }
  std::vector<RoutingShape>& get_routing_shape_list() { return _routing_shape_list; }
  std::vector<std::pair<int32_t, int32_t>>& get_via_shape_idx_pair_list() { return _via_shape_idx_pair_list; }
  std::map<std::string, std::vector<int32_t>>& get_terminal_shape_idx_map() { return _terminal_shape_idx_map; }
  // const getter
  const std::string& get_driver_terminal_name() const { return _driver_terminal_name; }
  int32_t get_terminal_routing_shape_num() const { return _terminal_routing_shape_num; }
  const std::vector<RoutingShape>& get_routing_shape_list() const { return _routing_shape_list; }
  const std::vector<std::pair<int32_t, int32_t>>& get_via_shape_idx_pair_list() const { return _via_shape_idx_pair_list; }
  const std::map<std::string, std::vector<int32_t>>& get_terminal_shape_idx_map() const { return _terminal_shape_idx_map; }
  // setter
  void set_driver_terminal_name(const std::string& driver_terminal_name) { _driver_terminal_name = driver_terminal_name; }
  void set_terminal_routing_shape_num(int32_t terminal_routing_shape_num) { _terminal_routing_shape_num = terminal_routing_shape_num; }

 private:
  std::string _driver_terminal_name;
  int32_t _terminal_routing_shape_num = 0;
  std::vector<RoutingShape> _routing_shape_list;
  std::vector<std::pair<int32_t, int32_t>> _via_shape_idx_pair_list;
  std::map<std::string, std::vector<int32_t>> _terminal_shape_idx_map;
};

}  // namespace ilvs
