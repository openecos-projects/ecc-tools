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

#include "EMIRHeader.hpp"
#include "PowerEdge.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"

namespace iemir {

class PowerGraph
{
 public:
  PowerGraph() = default;
  ~PowerGraph() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  PowerNetType get_net_type() { return _net_type; }
  std::vector<PowerNode>& get_node_list() { return _node_list; }
  std::vector<PowerEdge>& get_edge_list() { return _edge_list; }
  std::map<uint64_t, std::vector<std::size_t>>& get_instance_node_id_list_map() { return _instance_node_id_list_map; }
  std::vector<std::size_t>& get_source_node_id_list() { return _source_node_id_list; }
  std::vector<std::pair<std::size_t, std::size_t>>& get_node_equivalence_pair_list() { return _node_equivalence_pair_list; }
  bool get_is_connected() { return _is_connected; }
  double get_source_voltage() { return _source_voltage; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_net_type(PowerNetType net_type) { _net_type = net_type; }
  void set_is_connected(bool is_connected) { _is_connected = is_connected; }
  void set_source_voltage(double source_voltage) { _source_voltage = source_voltage; }
  // function
  void add_node_equivalence(std::size_t first_node_id, std::size_t second_node_id)
  {
    if (first_node_id != second_node_id) {
      _node_equivalence_pair_list.emplace_back(first_node_id, second_node_id);
    }
  }

  [[nodiscard]] std::vector<std::size_t> node_canonical_id_list() const
  {
    std::vector<std::size_t> parent(_node_list.size());
    for (std::size_t node_id = 0; node_id < parent.size(); node_id++) {
      parent[node_id] = node_id;
    }
    const auto find = [&parent](const auto& self, std::size_t node_id) -> std::size_t {
      if (parent[node_id] != node_id) {
        parent[node_id] = self(self, parent[node_id]);
      }
      return parent[node_id];
    };
    for (const auto [first_node_id, second_node_id] : _node_equivalence_pair_list) {
      const auto first_root = find(find, first_node_id);
      const auto second_root = find(find, second_node_id);
      if (first_root != second_root) {
        parent[second_root] = first_root;
      }
    }
    for (std::size_t node_id = 0; node_id < parent.size(); node_id++) {
      parent[node_id] = find(find, node_id);
    }
    return parent;
  }

 private:
  std::string _net_name;
  PowerNetType _net_type = PowerNetType::kNone;
  std::vector<PowerNode> _node_list;
  std::vector<PowerEdge> _edge_list;
  std::map<uint64_t, std::vector<std::size_t>> _instance_node_id_list_map;
  std::vector<std::size_t> _source_node_id_list;
  // Ideal electrical equivalence for geometry within one physical pin PORT.
  // It is intentionally not represented as a zero-resistance PowerEdge:
  // the IR solver works on the corresponding quotient graph instead.
  std::vector<std::pair<std::size_t, std::size_t>> _node_equivalence_pair_list;
  bool _is_connected = false;
  double _source_voltage = 0.0;
};

}  // namespace iemir
