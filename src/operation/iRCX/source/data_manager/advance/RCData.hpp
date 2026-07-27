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

#include "CouplingCapEntry.hpp"
#include "CornerNetPool.hpp"
#include "CouplingKey.hpp"
#include "CouplingKeyHash.hpp"
#include "TopoPool.hpp"

namespace ircx {

class RCData
{
 public:
  RCData() = default;
  ~RCData() = default;
  // getter
  int32_t get_corner_num() const { return _corner_num; }
  int32_t get_net_num() const { return _net_num; }
  CornerNetPool<std::vector<double>>& get_corner_net_resistance_pool() { return _corner_net_resistance_pool; }
  CornerNetPool<std::vector<double>>& get_corner_net_ground_capacitance_pool() { return _corner_net_ground_capacitance_pool; }
  std::vector<std::vector<CouplingCapEntry>>& get_net_coupling_cap_entry_list() { return _net_coupling_cap_entry_list; }
  std::unordered_map<CouplingKey, std::vector<double>, CouplingKeyHash>& get_merged_coupling_capacitance_map()
  {
    return _merged_coupling_capacitance_map;
  }
  // setter
  void set_corner_num(int32_t corner_num) { _corner_num = corner_num; }
  void set_net_num(int32_t net_num) { _net_num = net_num; }
  void set_corner_net_resistance_pool(const CornerNetPool<std::vector<double>>& corner_net_resistance_pool)
  {
    _corner_net_resistance_pool = corner_net_resistance_pool;
  }
  void set_corner_net_ground_capacitance_pool(const CornerNetPool<std::vector<double>>& corner_net_ground_capacitance_pool)
  {
    _corner_net_ground_capacitance_pool = corner_net_ground_capacitance_pool;
  }
  void set_net_coupling_cap_entry_list(const std::vector<std::vector<CouplingCapEntry>>& net_coupling_cap_entry_list)
  {
    _net_coupling_cap_entry_list = net_coupling_cap_entry_list;
  }
  void set_merged_coupling_capacitance_map(
      const std::unordered_map<CouplingKey, std::vector<double>, CouplingKeyHash>& merged_coupling_capacitance_map)
  {
    _merged_coupling_capacitance_map = merged_coupling_capacitance_map;
  }
  // function
  void init(int32_t corner_num, int32_t net_num, TopoPool& topo_pool)
  {
    _corner_num = corner_num;
    _net_num = net_num;
    _corner_net_resistance_pool.init(_corner_num, _net_num);
    _corner_net_ground_capacitance_pool.init(_corner_num, _net_num);
    _net_coupling_cap_entry_list.assign(_net_num, std::vector<CouplingCapEntry>());
    _merged_coupling_capacitance_map.clear();

    for (int32_t corner_idx = 0; corner_idx < _corner_num; ++corner_idx) {
      for (int32_t net_idx = 0; net_idx < _net_num; ++net_idx) {
        CornerNetIdx corner_net_idx(corner_idx, net_idx);
        int32_t edge_num = static_cast<int32_t>(topo_pool.get_net_edge_list(net_idx).size());
        _corner_net_resistance_pool.get_item(corner_net_idx).assign(edge_num, 0.0);
        _corner_net_ground_capacitance_pool.get_item(corner_net_idx).assign(edge_num, 0.0);
      }
    }
  }
  std::vector<double>& get_corner_net_resistance_list(CornerNetIdx corner_net_idx)
  {
    return _corner_net_resistance_pool.get_item(corner_net_idx);
  }
  std::vector<double>& get_corner_net_ground_capacitance_list(CornerNetIdx corner_net_idx)
  {
    return _corner_net_ground_capacitance_pool.get_item(corner_net_idx);
  }
  void append_net_coupling_cap_entry(int32_t net_idx, int32_t first_edge_idx, int32_t second_edge_idx, int32_t corner_idx,
                                     double coupling_capacitance)
  {
    _net_coupling_cap_entry_list[net_idx].emplace_back(first_edge_idx, second_edge_idx, corner_idx, coupling_capacitance);
  }
  void merge_net_coupling_cap_entry_list()
  {
    for (std::vector<CouplingCapEntry>& coupling_cap_entry_list : _net_coupling_cap_entry_list) {
      for (CouplingCapEntry& coupling_capacitance_entry : coupling_cap_entry_list) {
        CouplingKey coupling_key(coupling_capacitance_entry.get_first_edge_idx(), coupling_capacitance_entry.get_second_edge_idx());
        if (_merged_coupling_capacitance_map.count(coupling_key) == 0) {
          _merged_coupling_capacitance_map[coupling_key].resize(_corner_num, 0.0);
        }
        _merged_coupling_capacitance_map[coupling_key][coupling_capacitance_entry.get_corner_idx()]
            += coupling_capacitance_entry.get_coupling_capacitance();
      }
      coupling_cap_entry_list.clear();
    }
  }

 private:
  int32_t _corner_num = 0;
  int32_t _net_num = 0;
  CornerNetPool<std::vector<double>> _corner_net_resistance_pool;
  CornerNetPool<std::vector<double>> _corner_net_ground_capacitance_pool;
  std::vector<std::vector<CouplingCapEntry>> _net_coupling_cap_entry_list;
  std::unordered_map<CouplingKey, std::vector<double>, CouplingKeyHash> _merged_coupling_capacitance_map;
};

}  // namespace ircx
