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

#include "Net.hpp"
#include "RCXHeader.hpp"
#include "RoutingLayer.hpp"

namespace ircx {

class LayoutData
{
 public:
  LayoutData() = default;
  ~LayoutData() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  GTLRectInt& get_die_shape() { return _die_shape; }
  int32_t get_dbu_per_micron() const { return _dbu_per_micron; }
  std::map<int32_t, RoutingLayer>& get_routing_layer_map() { return _routing_layer_map; }
  std::vector<Net>& get_net_list() { return _net_list; }
  Net& get_special_net() { return _special_net; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_die_shape(const GTLRectInt& die_shape) { _die_shape = die_shape; }
  void set_dbu_per_micron(int32_t dbu_per_micron) { _dbu_per_micron = dbu_per_micron; }
  void set_routing_layer_map(const std::map<int32_t, RoutingLayer>& routing_layer_map) { _routing_layer_map = routing_layer_map; }
  void set_net_list(const std::vector<Net>& net_list) { _net_list = net_list; }
  void set_special_net(const Net& special_net) { _special_net = special_net; }
  // function
  int32_t get_regular_net_num() const { return static_cast<int32_t>(_net_list.size()); }
  bool get_is_empty() const
  {
    return _net_list.empty() && _special_net.get_segment_list().empty() && _special_net.get_patch_list().empty()
           && _special_net.get_via_list().empty() && _special_net.get_pin_list().empty();
  }

 private:
  std::string _design_name;
  GTLRectInt _die_shape;
  int32_t _dbu_per_micron = -1;
  std::map<int32_t, RoutingLayer> _routing_layer_map;
  std::vector<Net> _net_list;
  Net _special_net;
};

}  // namespace ircx
