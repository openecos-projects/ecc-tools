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

#include "OffsetRange.hpp"
#include "TopoEdge.hpp"
#include "TopoNode.hpp"

namespace ircx {

class TopoPool
{
 public:
  TopoPool() = default;
  ~TopoPool() = default;
  // getter
  std::vector<TopoNode>& get_node_pool() { return _node_pool; }
  std::vector<TopoEdge>& get_edge_pool() { return _edge_pool; }
  std::vector<TopoEdge>& get_special_edge_pool() { return _special_edge_pool; }
  TopoNode& get_node(int32_t node_idx) { return _node_pool[node_idx]; }
  TopoEdge& get_edge(int32_t edge_idx) { return _edge_pool[edge_idx]; }
  std::span<TopoNode> get_net_node_list(int32_t net_idx)
  {
    OffsetRange net_node_range = _net_node_range_list[net_idx];
    return std::span<TopoNode>(_node_pool.data() + net_node_range.get_offset(), net_node_range.get_count());
  }
  std::span<TopoEdge> get_net_edge_list(int32_t net_idx)
  {
    OffsetRange net_edge_range = _net_edge_range_list[net_idx];
    return std::span<TopoEdge>(_edge_pool.data() + net_edge_range.get_offset(), net_edge_range.get_count());
  }
  OffsetRange get_net_node_range(int32_t net_idx) { return _net_node_range_list[net_idx]; }
  OffsetRange get_net_edge_range(int32_t net_idx) { return _net_edge_range_list[net_idx]; }
  // setter
  // function
  int32_t get_node_idx(int32_t net_idx, int32_t local_node_idx)
  {
    return _net_node_range_list[net_idx].get_offset() + local_node_idx;
  }
  int32_t get_edge_idx(int32_t net_idx, int32_t local_edge_idx)
  {
    return _net_edge_range_list[net_idx].get_offset() + local_edge_idx;
  }
  void reserve(int32_t net_count, int32_t node_count, int32_t edge_count)
  {
    _net_node_range_list.reserve(net_count);
    _net_edge_range_list.reserve(net_count);
    _node_pool.reserve(node_count);
    _edge_pool.reserve(edge_count);
  }
  void add_net(std::vector<TopoNode> node_list, std::vector<TopoEdge> edge_list)
  {
    int32_t node_offset = static_cast<int32_t>(_node_pool.size());
    int32_t node_count = static_cast<int32_t>(node_list.size());
    int32_t edge_offset = static_cast<int32_t>(_edge_pool.size());
    int32_t edge_count = static_cast<int32_t>(edge_list.size());

    assign_node_idx(node_list);
    assign_edge_idx(edge_list, false);
    _net_node_range_list.emplace_back(node_offset, node_count);
    _net_edge_range_list.emplace_back(edge_offset, edge_count);
    _node_pool.insert(_node_pool.end(), std::make_move_iterator(node_list.begin()), std::make_move_iterator(node_list.end()));
    _edge_pool.insert(_edge_pool.end(), std::make_move_iterator(edge_list.begin()), std::make_move_iterator(edge_list.end()));
  }
  void add_special_edge_list(std::vector<TopoEdge> edge_list)
  {
    assign_edge_idx(edge_list, true);
    _special_edge_pool.insert(_special_edge_pool.end(), std::make_move_iterator(edge_list.begin()), std::make_move_iterator(edge_list.end()));
  }

 private:
  void assign_node_idx(std::vector<TopoNode>& node_list)
  {
    for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(node_list.size()); ++node_idx) {
      node_list[node_idx].set_node_idx(node_idx);
    }
  }
  void assign_edge_idx(std::vector<TopoEdge>& edge_list, bool is_special_net)
  {
    for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
      edge_list[edge_idx].set_edge_idx(edge_idx);
      edge_list[edge_idx].set_is_special_net(is_special_net);
    }
  }

  std::vector<TopoNode> _node_pool;
  std::vector<OffsetRange> _net_node_range_list;
  std::vector<TopoEdge> _edge_pool;
  std::vector<OffsetRange> _net_edge_range_list;
  std::vector<TopoEdge> _special_edge_pool;
};

}  // namespace ircx
