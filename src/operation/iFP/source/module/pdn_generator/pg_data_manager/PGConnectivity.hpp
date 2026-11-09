// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"

namespace ifp {

class PGConnectivity
{
 public:
  explicit PGConnectivity(int32_t segment_num) : _parent_list(segment_num), _size_list(segment_num, 1)
  {
    for (int32_t idx = 0; idx < segment_num; idx++) {
      _parent_list[idx] = idx;
    }
  }
  ~PGConnectivity() = default;
  // function
  int32_t getRoot(int32_t idx)
  {
    while (_parent_list[idx] != idx) {
      _parent_list[idx] = _parent_list[_parent_list[idx]];
      idx = _parent_list[idx];
    }
    return idx;
  }
  void connect(int32_t first, int32_t second)
  {
    first = getRoot(first);
    second = getRoot(second);
    if (first == second) {
      return;
    }
    if (_size_list[first] < _size_list[second]) {
      std::swap(first, second);
    }
    _parent_list[second] = first;
    _size_list[first] += _size_list[second];
  }

 private:
  std::vector<int32_t> _parent_list;
  std::vector<int32_t> _size_list;
};

}  // namespace ifp
