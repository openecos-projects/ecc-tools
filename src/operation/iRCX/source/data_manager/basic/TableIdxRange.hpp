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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "RCXHeader.hpp"

namespace ircx {

class TableIdxRange
{
 public:
  TableIdxRange() = default;
  TableIdxRange(int32_t lower_idx, int32_t upper_idx) : _lower_idx(lower_idx), _upper_idx(upper_idx) {}
  ~TableIdxRange() = default;
  // getter
  int32_t get_lower_idx() const { return _lower_idx; }
  int32_t get_upper_idx() const { return _upper_idx; }
  // setter
  void set_lower_idx(int32_t lower_idx) { _lower_idx = lower_idx; }
  void set_upper_idx(int32_t upper_idx) { _upper_idx = upper_idx; }
  // function

 private:
  int32_t _lower_idx = -1;
  int32_t _upper_idx = -1;
};

}  // namespace ircx
