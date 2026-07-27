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

#include "CouplingKey.hpp"

namespace ircx {

class CouplingKeyHash
{
 public:
  CouplingKeyHash() = default;
  ~CouplingKeyHash() = default;
  // getter
  // setter
  // function
  size_t operator()(const CouplingKey& coupling_key) const
  {
    size_t seed = std::hash<int32_t>()(coupling_key.get_first_edge_idx());
    size_t value = std::hash<int32_t>()(coupling_key.get_second_edge_idx());
    size_t magic = sizeof(size_t) == 8 ? static_cast<size_t>(0x9e3779b97f4a7c15ull) : static_cast<size_t>(0x9e3779b9ul);
    return seed ^ (value + magic + (seed << 6) + (seed >> 2));
  }
};

}  // namespace ircx
