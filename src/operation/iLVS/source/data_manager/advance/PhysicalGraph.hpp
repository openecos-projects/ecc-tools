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

namespace ilvs {

struct PhysicalShapeRef
{
  int32_t net_id = -1;
  int32_t routing_shape_idx = -1;
};

class PhysicalGraph
{
 public:
  PhysicalGraph() = default;
  // getter
  std::map<int32_t, std::vector<std::string>>& get_component_net_name_map() { return _component_net_name_map; }
  std::map<int32_t, std::vector<Shape>>& get_component_shape_map() { return _component_shape_map; }
  std::map<std::string, int32_t>& get_terminal_component_map() { return _terminal_component_map; }
  std::map<std::string, NetRoutingGraph>& get_net_routing_graph_map() { return _net_routing_graph_map; }
  std::map<std::string, std::vector<int32_t>>& get_net_routing_shape_component_id_list_map() { return _net_routing_shape_component_id_list_map; }
  std::set<std::string>& get_power_net_name_set() { return _power_net_name_set; }
  std::set<std::string>& get_ground_net_name_set() { return _ground_net_name_set; }
  std::unordered_map<std::string, std::string>& get_power_instance_pin_net_map() { return _power_instance_pin_net_map; }
  std::unordered_map<std::string, std::string>& get_ground_instance_pin_net_map() { return _ground_instance_pin_net_map; }
  std::vector<std::vector<int32_t>>& get_component_net_id_list() { return _component_net_id_list; }
  std::vector<std::vector<PhysicalShapeRef>>& get_component_shape_ref_list() { return _component_shape_ref_list; }
  // const getter
  const std::map<int32_t, std::vector<std::string>>& get_component_net_name_map() const { return _component_net_name_map; }
  const std::map<int32_t, std::vector<Shape>>& get_component_shape_map() const { return _component_shape_map; }
  const std::map<std::string, int32_t>& get_terminal_component_map() const { return _terminal_component_map; }
  const std::map<std::string, NetRoutingGraph>& get_net_routing_graph_map() const { return _net_routing_graph_map; }
  const std::map<std::string, std::vector<int32_t>>& get_net_routing_shape_component_id_list_map() const { return _net_routing_shape_component_id_list_map; }
  const std::set<std::string>& get_power_net_name_set() const { return _power_net_name_set; }
  const std::set<std::string>& get_ground_net_name_set() const { return _ground_net_name_set; }
  const std::unordered_map<std::string, std::string>& get_power_instance_pin_net_map() const { return _power_instance_pin_net_map; }
  const std::unordered_map<std::string, std::string>& get_ground_instance_pin_net_map() const { return _ground_instance_pin_net_map; }
  const std::vector<std::vector<int32_t>>& get_component_net_id_list() const { return _component_net_id_list; }
  const std::vector<std::vector<PhysicalShapeRef>>& get_component_shape_ref_list() const { return _component_shape_ref_list; }
  bool has_optimized_component_data() const { return _optimized_component_data_valid; }
  int32_t getOrCreateNetId(const std::string& net_name)
  {
    auto iter = _net_id_map.find(net_name);
    if (iter != _net_id_map.end()) {
      return iter->second;
    }
    int32_t net_id = static_cast<int32_t>(_net_name_list.size());
    _net_id_map.emplace(net_name, net_id);
    _net_name_list.push_back(net_name);
    return net_id;
  }
  std::vector<std::string> get_component_net_name_list(int32_t component_id) const
  {
    if (_optimized_component_data_valid && component_id >= 0 && component_id < static_cast<int32_t>(_component_net_id_list.size())) {
      std::vector<std::string> net_name_list;
      net_name_list.reserve(_component_net_id_list[component_id].size());
      for (int32_t net_id : _component_net_id_list[component_id]) {
        if (net_id >= 0 && net_id < static_cast<int32_t>(_net_name_list.size())) {
          net_name_list.push_back(_net_name_list[net_id]);
        }
      }
      return net_name_list;
    }
    auto iter = _component_net_name_map.find(component_id);
    return iter == _component_net_name_map.end() ? std::vector<std::string>() : iter->second;
  }
  std::vector<Shape> get_component_shape_list(int32_t component_id) const
  {
    if (_optimized_component_data_valid && component_id >= 0 && component_id < static_cast<int32_t>(_component_shape_ref_list.size())) {
      std::vector<Shape> shape_list;
      shape_list.reserve(_component_shape_ref_list[component_id].size());
      int32_t current_net_id = -1;
      const std::vector<RoutingShape>* current_routing_shape_list = nullptr;
      for (const PhysicalShapeRef& shape_ref : _component_shape_ref_list[component_id]) {
        if (shape_ref.net_id < 0 || shape_ref.net_id >= static_cast<int32_t>(_net_name_list.size())) {
          continue;
        }
        if (shape_ref.net_id != current_net_id) {
          current_net_id = shape_ref.net_id;
          auto graph_iter = _net_routing_graph_map.find(_net_name_list[shape_ref.net_id]);
          current_routing_shape_list
              = graph_iter == _net_routing_graph_map.end() ? nullptr : &graph_iter->second.get_routing_shape_list();
        }
        if (current_routing_shape_list == nullptr) {
          continue;
        }
        if (shape_ref.routing_shape_idx >= 0 && shape_ref.routing_shape_idx < static_cast<int32_t>(current_routing_shape_list->size())) {
          shape_list.push_back((*current_routing_shape_list)[shape_ref.routing_shape_idx].get_shape());
        }
      }
      return shape_list;
    }
    auto iter = _component_shape_map.find(component_id);
    return iter == _component_shape_map.end() ? std::vector<Shape>() : iter->second;
  }
  const std::string& get_net_name(int32_t net_id) const { return _net_name_list.at(net_id); }
  void set_optimized_component_data_valid(bool valid) { _optimized_component_data_valid = valid; }
  // function
  void resetDerivedData()
  {
    _component_net_name_map.clear();
    _component_shape_map.clear();
    _terminal_component_map.clear();
    _net_routing_shape_component_id_list_map.clear();
    _net_id_map.clear();
    _net_name_list.clear();
    _component_net_id_list.clear();
    _component_shape_ref_list.clear();
    _optimized_component_data_valid = false;
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
  std::map<std::string, std::vector<int32_t>> _net_routing_shape_component_id_list_map;
  std::set<std::string> _power_net_name_set;
  std::set<std::string> _ground_net_name_set;
  std::unordered_map<std::string, std::string> _power_instance_pin_net_map;
  std::unordered_map<std::string, std::string> _ground_instance_pin_net_map;
  std::unordered_map<std::string, int32_t> _net_id_map;
  std::vector<std::string> _net_name_list;
  std::vector<std::vector<int32_t>> _component_net_id_list;
  std::vector<std::vector<PhysicalShapeRef>> _component_shape_ref_list;
  bool _optimized_component_data_valid = false;
};

}  // namespace ilvs
