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
namespace ilvs {

class DisjointSet
{
 public:
  explicit DisjointSet(const int32_t size) : _parent(size), _rank(size, 0) { std::iota(_parent.begin(), _parent.end(), 0); }
  ~DisjointSet() = default;
  // function
  int32_t find(const int32_t node)
  {
    if (_parent[node] != node) {
      _parent[node] = find(_parent[node]);
    }
    return _parent[node];
  }
  bool unite(int32_t first_node, int32_t second_node)
  {
    first_node = find(first_node);
    second_node = find(second_node);
    if (first_node == second_node) {
      return false;
    }
    if (_rank[first_node] < _rank[second_node]) {
      std::swap(first_node, second_node);
    }
    _parent[second_node] = first_node;
    if (_rank[first_node] == _rank[second_node]) {
      _rank[first_node]++;
    }
    return true;
  }

 private:
  std::vector<int32_t> _parent;
  std::vector<int32_t> _rank;
};

}  // namespace ilvs
