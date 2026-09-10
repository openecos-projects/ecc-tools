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

#include "ACSegmentTreeNode.hpp"
#include "ZHHeader.hpp"

namespace izh {

class ACSegmentTree
{
 public:
  void init(const std::vector<int64_t>& y_coords)
  {
    _y_coords = y_coords;
    _interval_num = static_cast<int>(_y_coords.size()) - 1;
    _nodes.assign(static_cast<size_t>(4) * std::max(1, _interval_num), ACSegmentTreeNode());
  }

  long double coveredLength() const
  {
    if (_interval_num <= 0) {
      return 0.0L;
    }
    return _nodes[1].length;
  }

  int intervalCount() const
  {
    if (_interval_num <= 0) {
      return 0;
    }
    return _nodes[1].interval_num;
  }

  void update(const int low_idx, const int high_idx, const int delta)
  {
    if (_interval_num <= 0 || low_idx > high_idx) {
      return;
    }
    updateImpl(1, 0, _interval_num - 1, low_idx, high_idx, delta);
  }

  long double queryCovered(const int low_idx, const int high_idx) const
  {
    if (_interval_num <= 0 || low_idx > high_idx) {
      return 0.0L;
    }
    return queryCoveredImpl(1, 0, _interval_num - 1, low_idx, high_idx);
  }

 private:
  long double getSegmentLength(const int low_idx, const int high_idx) const
  {
    return static_cast<long double>(_y_coords[high_idx + 1] - _y_coords[low_idx]);
  }

  void pull(const int node_idx, const int low_idx, const int high_idx)
  {
    ACSegmentTreeNode& node = _nodes[node_idx];
    if (node.cover > 0) {
      node.length = getSegmentLength(low_idx, high_idx);
      node.interval_num = 1;
      node.left_covered = true;
      node.right_covered = true;
      return;
    }

    if (low_idx == high_idx) {
      node.length = 0.0L;
      node.interval_num = 0;
      node.left_covered = false;
      node.right_covered = false;
      return;
    }

    const int left_child_idx = node_idx * 2;
    const int right_child_idx = left_child_idx + 1;
    const ACSegmentTreeNode& left_child = _nodes[left_child_idx];
    const ACSegmentTreeNode& right_child = _nodes[right_child_idx];
    node.length = left_child.length + right_child.length;
    node.interval_num = left_child.interval_num + right_child.interval_num -
                        ((left_child.right_covered && right_child.left_covered) ? 1 : 0);
    node.left_covered = left_child.left_covered;
    node.right_covered = right_child.right_covered;
  }

  void updateImpl(const int node_idx, const int low_idx, const int high_idx, const int query_low_idx, const int query_high_idx,
                  const int delta)
  {
    if (query_high_idx < low_idx || high_idx < query_low_idx) {
      return;
    }

    if (query_low_idx <= low_idx && high_idx <= query_high_idx) {
      _nodes[node_idx].cover += delta;
      if (_nodes[node_idx].cover < 0) {
        _nodes[node_idx].cover = 0;
      }
      pull(node_idx, low_idx, high_idx);
      return;
    }

    const int middle_idx = (low_idx + high_idx) / 2;
    updateImpl(node_idx * 2, low_idx, middle_idx, query_low_idx, query_high_idx, delta);
    updateImpl(node_idx * 2 + 1, middle_idx + 1, high_idx, query_low_idx, query_high_idx, delta);
    pull(node_idx, low_idx, high_idx);
  }

  long double queryCoveredImpl(const int node_idx, const int low_idx, const int high_idx, const int query_low_idx,
                               const int query_high_idx) const
  {
    if (query_high_idx < low_idx || high_idx < query_low_idx) {
      return 0.0L;
    }

    if (query_low_idx <= low_idx && high_idx <= query_high_idx) {
      return _nodes[node_idx].length;
    }

    if (_nodes[node_idx].cover > 0) {
      const int clipped_low_idx = std::max(low_idx, query_low_idx);
      const int clipped_high_idx = std::min(high_idx, query_high_idx);
      if (clipped_low_idx > clipped_high_idx) {
        return 0.0L;
      }
      return static_cast<long double>(_y_coords[clipped_high_idx + 1] - _y_coords[clipped_low_idx]);
    }

    if (low_idx == high_idx) {
      return 0.0L;
    }

    const int middle_idx = (low_idx + high_idx) / 2;
    return queryCoveredImpl(node_idx * 2, low_idx, middle_idx, query_low_idx, query_high_idx) +
           queryCoveredImpl(node_idx * 2 + 1, middle_idx + 1, high_idx, query_low_idx, query_high_idx);
  }

  std::vector<int64_t> _y_coords;
  int _interval_num = 0;
  std::vector<ACSegmentTreeNode> _nodes;
};

}  // namespace izh
