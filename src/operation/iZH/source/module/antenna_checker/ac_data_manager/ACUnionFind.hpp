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

#include "ACGraphNode.hpp"
#include "ACUFNode.hpp"
#include "Utility.hpp"

namespace izh {

class ACUnionFind
{
 public:
  ACUnionFind(const int node_num, const std::vector<ACGraphNode>& nodes)
  {
    d.resize(node_num);

    for (int node_idx = 0; node_idx < node_num; ++node_idx) {
      d[node_idx].parent = node_idx;
      d[node_idx].rank = 0;
      d[node_idx].gate_area = nodes[node_idx].gate_area;
      d[node_idx].diff_area = nodes[node_idx].diff_area;
      d[node_idx].diff_connected = nodes[node_idx].provides_diff ||
                                   !Utility::equalDoubleByError(nodes[node_idx].diff_area, 0.0, ZH_ERROR);
      d[node_idx].cum_area_num = 0.0;
      d[node_idx].cum_side_num = 0.0;
      d[node_idx].cum_cut_num = 0.0;
    }
  }

  int find(const int node_idx)
  {
    if (d[node_idx].parent == node_idx) {
      return node_idx;
    }

    d[node_idx].parent = find(d[node_idx].parent);
    return d[node_idx].parent;
  }

  void merge(int first_node_idx, int second_node_idx)
  {
    int first_root = find(first_node_idx);
    int second_root = find(second_node_idx);

    if (first_root == second_root) {
      return;
    }

    if (d[first_root].rank < d[second_root].rank) {
      std::swap(first_root, second_root);
    }

    d[second_root].parent = first_root;

    if (d[first_root].rank == d[second_root].rank) {
      ++d[first_root].rank;
    }

    d[first_root].gate_area += d[second_root].gate_area;
    d[first_root].diff_area += d[second_root].diff_area;
    d[first_root].diff_connected = d[first_root].diff_connected || d[second_root].diff_connected;
    d[first_root].cum_area_num += d[second_root].cum_area_num;
    d[first_root].cum_side_num += d[second_root].cum_side_num;
    d[first_root].cum_cut_num += d[second_root].cum_cut_num;
  }

  std::vector<ACUFNode> d;
};

}  // namespace izh
