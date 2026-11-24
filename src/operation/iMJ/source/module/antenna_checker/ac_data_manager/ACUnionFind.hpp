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

#include "ACGraphNode.hpp"
#include "ACUFNode.hpp"
#include "Utility.hpp"

namespace imj {

class ACUnionFind
{
 public:
  ACUnionFind(const int node_num, const std::vector<ACGraphNode>& nodes)
  {
    d.resize(node_num);

    for (int node_idx = 0; node_idx < node_num; ++node_idx) {
      d[node_idx].parent = node_idx;
      d[node_idx].rank = 0;
      if (!nodes[node_idx].is_conductor && nodes[node_idx].gate_area > 0.0) {
        d[node_idx].gate_node_idx = node_idx;
      }

      const bool has_diff = nodes[node_idx].provides_diff || !Utility::equalDoubleByError(nodes[node_idx].diff_area, 0.0, MJ_ERROR);
      if (nodes[node_idx].pin_id >= 0 && nodes[node_idx].layer_order >= 0) {
        ACLayerPinContrib contrib;
        if (!Utility::equalDoubleByError(nodes[node_idx].gate_area, 0.0, MJ_ERROR)) {
          contrib.gate_order = nodes[node_idx].layer_order;
          contrib.gate_area = nodes[node_idx].gate_area;
        }
        if (has_diff) {
          contrib.diff_order = nodes[node_idx].layer_order;
          contrib.diff_area = nodes[node_idx].diff_area;
          contrib.provides_diff = true;
        }
        d[node_idx].qualified_pins.emplace(nodes[node_idx].pin_id, contrib);
        d[node_idx].qualified_gate_area = contrib.gate_area;
        d[node_idx].qualified_diff_area = contrib.diff_area;
        d[node_idx].qualified_diff_count = contrib.provides_diff ? 1 : 0;
      } else {
        d[node_idx].unqualified_gate_area = nodes[node_idx].gate_area;
        d[node_idx].unqualified_diff_area = nodes[node_idx].diff_area;
        d[node_idx].unqualified_diff_connected = has_diff;
      }

      refreshTotals(d[node_idx]);
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

    // Keep a representative gate without rescanning the net at each layer event.
    if (d[second_root].gate_node_idx >= 0 && (d[first_root].gate_node_idx < 0 || d[second_root].gate_node_idx < d[first_root].gate_node_idx)) {
      d[first_root].gate_node_idx = d[second_root].gate_node_idx;
    }

    d[first_root].unqualified_gate_area += d[second_root].unqualified_gate_area;
    d[first_root].unqualified_diff_area += d[second_root].unqualified_diff_area;
    d[first_root].unqualified_diff_connected = d[first_root].unqualified_diff_connected || d[second_root].unqualified_diff_connected;
    mergeQualifiedPins(d[first_root], d[second_root]);
    refreshTotals(d[first_root]);

    d[first_root].cum_area_num += d[second_root].cum_area_num;
    d[first_root].cum_side_num += d[second_root].cum_side_num;
    d[first_root].cum_cut_num += d[second_root].cum_cut_num;
  }

  std::vector<ACUFNode> d;

 private:
  static void mergeQualifiedPins(ACUFNode& dst, ACUFNode& src)
  {
    if (src.qualified_pins.empty()) {
      return;
    }
    if (dst.qualified_pins.size() < src.qualified_pins.size()) {
      std::swap(dst.qualified_pins, src.qualified_pins);
      std::swap(dst.qualified_gate_area, src.qualified_gate_area);
      std::swap(dst.qualified_diff_area, src.qualified_diff_area);
      std::swap(dst.qualified_diff_count, src.qualified_diff_count);
    }
    for (const auto& [pin_id, incoming] : src.qualified_pins) {
      auto [it, inserted] = dst.qualified_pins.emplace(pin_id, incoming);
      if (inserted) {
        dst.qualified_gate_area += incoming.gate_area;
        dst.qualified_diff_area += incoming.diff_area;
        if (incoming.provides_diff) {
          ++dst.qualified_diff_count;
        }
        continue;
      }
      ACLayerPinContrib& existing = it->second;
      if (incoming.gate_order > existing.gate_order
          || (incoming.gate_order == existing.gate_order && incoming.gate_area > existing.gate_area)) {
        dst.qualified_gate_area += (incoming.gate_area - existing.gate_area);
        existing.gate_order = incoming.gate_order;
        existing.gate_area = incoming.gate_area;
      }
      if (incoming.diff_order > existing.diff_order
          || (incoming.diff_order == existing.diff_order && incoming.diff_area > existing.diff_area)) {
        dst.qualified_diff_area += (incoming.diff_area - existing.diff_area);
        if (incoming.provides_diff != existing.provides_diff) {
          dst.qualified_diff_count += incoming.provides_diff ? 1 : -1;
        }
        existing.diff_order = incoming.diff_order;
        existing.diff_area = incoming.diff_area;
        existing.provides_diff = incoming.provides_diff;
      }
    }
    src.qualified_pins.clear();
    src.qualified_gate_area = 0.0;
    src.qualified_diff_area = 0.0;
    src.qualified_diff_count = 0;
  }

  static void refreshTotals(ACUFNode& node)
  {
    node.gate_area = node.unqualified_gate_area + node.qualified_gate_area;
    node.diff_area = node.unqualified_diff_area + node.qualified_diff_area;
    node.diff_connected = node.unqualified_diff_connected || (node.qualified_diff_count > 0)
                          || !Utility::equalDoubleByError(node.diff_area, 0.0, MJ_ERROR);
  }
};

}  // namespace imj
