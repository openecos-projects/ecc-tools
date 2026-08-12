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

#include "RCXHeader.hpp"

namespace ircx {

class CornerNetIdx
{
 public:
  CornerNetIdx() = default;
  CornerNetIdx(int32_t corner_idx, int32_t net_idx)
  {
    _corner_idx = corner_idx;
    _net_idx = net_idx;
  }
  ~CornerNetIdx() = default;
  // getter
  int32_t get_corner_idx() const { return _corner_idx; }
  int32_t get_net_idx() const { return _net_idx; }
  // setter
  void set_corner_idx(int32_t corner_idx) { _corner_idx = corner_idx; }
  void set_net_idx(int32_t net_idx) { _net_idx = net_idx; }
  // function

 private:
  int32_t _corner_idx = -1;
  int32_t _net_idx = -1;
};

}  // namespace ircx
