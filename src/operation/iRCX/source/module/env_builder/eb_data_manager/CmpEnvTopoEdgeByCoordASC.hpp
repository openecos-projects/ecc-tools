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
#include "TopoEdge.hpp"

namespace ircx {

class CmpEnvTopoEdgeByCoordASC
{
 public:
  CmpEnvTopoEdgeByCoordASC() = default;
  ~CmpEnvTopoEdgeByCoordASC() = default;
  // getter
  // setter
  // function
  bool operator()(TopoEdge* lhs, TopoEdge* rhs) const
  {
    if (lhs == rhs) {
      return false;
    }
    if (lhs == nullptr || rhs == nullptr) {
      return lhs == nullptr;
    }
    if (lhs->get_line_segment().get_coord() != rhs->get_line_segment().get_coord()) {
      return lhs->get_line_segment().get_coord() < rhs->get_line_segment().get_coord();
    }
    if (lhs->get_is_special_net() != rhs->get_is_special_net()) {
      return lhs->get_is_special_net() < rhs->get_is_special_net();
    }
    if (lhs->get_net_idx() != rhs->get_net_idx()) {
      return lhs->get_net_idx() < rhs->get_net_idx();
    }
    return lhs->get_edge_idx() < rhs->get_edge_idx();
  }

};

}  // namespace ircx
