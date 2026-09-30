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
  std::vector<NetRoutingGraph>& get_net_routing_graph_list() { return _net_routing_graph_list; }
  std::set<std::string>& get_power_net_name_set() { return _power_net_name_set; }
  std::set<std::string>& get_ground_net_name_set() { return _ground_net_name_set; }
  std::unordered_map<std::string, std::string>& get_power_instance_pin_net_map() { return _power_instance_pin_net_map; }
  std::unordered_map<std::string, std::string>& get_ground_instance_pin_net_map() { return _ground_instance_pin_net_map; }
  std::unordered_map<int32_t, std::vector<int32_t>>& get_component_net_id_map() { return _component_net_id_map; }
  std::unordered_map<int32_t, std::vector<PhysicalShapeRef>>& get_component_shape_ref_map() { return _component_shape_ref_map; }
  std::vector<int32_t>& get_short_component_id_list() { return _short_component_id_list; }
  // const getter
  const std::map<int32_t, std::vector<std::string>>& get_component_net_name_map() const { return _component_net_name_map; }
  const std::map<int32_t, std::vector<Shape>>& get_component_shape_map() const { return _component_shape_map; }
  const std::map<std::string, int32_t>& get_terminal_component_map() const { return _terminal_component_map; }
  const std::vector<NetRoutingGraph>& get_net_routing_graph_list() const { return _net_routing_graph_list; }
  const std::set<std::string>& get_power_net_name_set() const { return _power_net_name_set; }
  const std::set<std::string>& get_ground_net_name_set() const { return _ground_net_name_set; }
  const std::unordered_map<std::string, std::string>& get_power_instance_pin_net_map() const { return _power_instance_pin_net_map; }
  const std::unordered_map<std::string, std::string>& get_ground_instance_pin_net_map() const { return _ground_instance_pin_net_map; }
  const std::unordered_map<int32_t, std::vector<int32_t>>& get_component_net_id_map() const { return _component_net_id_map; }
  const std::unordered_map<int32_t, std::vector<PhysicalShapeRef>>& get_component_shape_ref_map() const { return _component_shape_ref_map; }
  const std::vector<int32_t>& get_short_component_id_list() const { return _short_component_id_list; }
  bool has_optimized_component_data() const { return _optimized_component_data_valid; }
  int32_t getNetId(const std::string& net_name) const
  {
    auto iter = _net_id_map.find(net_name);
    return iter == _net_id_map.end() ? -1 : iter->second;
  }
  int32_t getOrCreateNetId(const std::string& net_name)
  {
    int32_t net_id = getNetId(net_name);
    if (net_id >= 0) {
      return net_id;
    }
    net_id = static_cast<int32_t>(_net_name_list.size());
    _net_id_map.emplace(net_name, net_id);
    _net_name_list.push_back(net_name);
    _net_routing_graph_list.emplace_back();
    return net_id;
  }
  void reserveNetNum(size_t net_num)
  {
    _net_id_map.reserve(net_num);
    _net_name_list.reserve(net_num);
    _net_routing_graph_list.reserve(net_num);
  }
  NetRoutingGraph& getOrCreateNetRoutingGraph(const std::string& net_name) { return _net_routing_graph_list[getOrCreateNetId(net_name)]; }
  NetRoutingGraph* getNetRoutingGraph(int32_t net_id)
  {
    return net_id >= 0 && net_id < static_cast<int32_t>(_net_routing_graph_list.size()) ? &_net_routing_graph_list[net_id] : nullptr;
  }
  const NetRoutingGraph* getNetRoutingGraph(int32_t net_id) const
  {
    return net_id >= 0 && net_id < static_cast<int32_t>(_net_routing_graph_list.size()) ? &_net_routing_graph_list[net_id] : nullptr;
  }
  NetRoutingGraph* getNetRoutingGraph(const std::string& net_name) { return getNetRoutingGraph(getNetId(net_name)); }
  const NetRoutingGraph* getNetRoutingGraph(const std::string& net_name) const { return getNetRoutingGraph(getNetId(net_name)); }
  std::vector<int32_t>& getOrCreateNetRoutingShapeComponentIdList(int32_t net_id)
  {
    return _net_routing_shape_component_id_list_map[net_id];
  }
  std::vector<int32_t>* getNetRoutingShapeComponentIdList(int32_t net_id)
  {
    auto iter = _net_routing_shape_component_id_list_map.find(net_id);
    return iter == _net_routing_shape_component_id_list_map.end() ? nullptr : &iter->second;
  }
  const std::vector<int32_t>* getNetRoutingShapeComponentIdList(int32_t net_id) const
  {
    auto iter = _net_routing_shape_component_id_list_map.find(net_id);
    return iter == _net_routing_shape_component_id_list_map.end() ? nullptr : &iter->second;
  }
  std::vector<std::string> get_component_net_name_list(int32_t component_id) const
  {
    auto component_iter = _component_net_id_map.find(component_id);
    if (_optimized_component_data_valid && component_iter != _component_net_id_map.end()) {
      std::vector<std::string> net_name_list;
      net_name_list.reserve(component_iter->second.size());
      for (int32_t net_id : component_iter->second) {
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
    auto component_iter = _component_shape_ref_map.find(component_id);
    if (_optimized_component_data_valid && component_iter != _component_shape_ref_map.end()) {
      std::vector<Shape> shape_list;
      shape_list.reserve(component_iter->second.size());
      int32_t current_net_id = -1;
      const std::vector<RoutingShape>* current_routing_shape_list = nullptr;
      for (const PhysicalShapeRef& shape_ref : component_iter->second) {
        if (shape_ref.net_id < 0 || shape_ref.net_id >= static_cast<int32_t>(_net_name_list.size())) {
          continue;
        }
        if (shape_ref.net_id != current_net_id) {
          current_net_id = shape_ref.net_id;
          current_routing_shape_list = &_net_routing_graph_list[shape_ref.net_id].get_routing_shape_list();
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
    _component_net_id_map.clear();
    _component_shape_ref_map.clear();
    _short_component_id_list.clear();
    _optimized_component_data_valid = false;
  }
  void reset()
  {
    resetDerivedData();
    _net_routing_graph_list.clear();
    _net_id_map.clear();
    _net_name_list.clear();
    _power_net_name_set.clear();
    _ground_net_name_set.clear();
    _power_instance_pin_net_map.clear();
    _ground_instance_pin_net_map.clear();
  }

 private:
  std::map<int32_t, std::vector<std::string>> _component_net_name_map;
  std::map<int32_t, std::vector<Shape>> _component_shape_map;
  std::map<std::string, int32_t> _terminal_component_map;
  std::vector<NetRoutingGraph> _net_routing_graph_list;
  std::unordered_map<int32_t, std::vector<int32_t>> _net_routing_shape_component_id_list_map;
  std::set<std::string> _power_net_name_set;
  std::set<std::string> _ground_net_name_set;
  std::unordered_map<std::string, std::string> _power_instance_pin_net_map;
  std::unordered_map<std::string, std::string> _ground_instance_pin_net_map;
  std::unordered_map<std::string, int32_t> _net_id_map;
  std::vector<std::string> _net_name_list;
  std::unordered_map<int32_t, std::vector<int32_t>> _component_net_id_map;
  std::unordered_map<int32_t, std::vector<PhysicalShapeRef>> _component_shape_ref_map;
  std::vector<int32_t> _short_component_id_list;
  bool _optimized_component_data_valid = false;
};

}  // namespace ilvs
