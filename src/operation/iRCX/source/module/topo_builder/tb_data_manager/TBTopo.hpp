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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "RCXHeader.hpp"
#include "TopoEdge.hpp"
#include "TopoNode.hpp"

namespace ircx {

class TBTopo
{
 public:
  TBTopo() = default;
  ~TBTopo() = default;
  // getter
  std::vector<TopoNode>& get_node_list() { return _node_list; }
  std::vector<TopoEdge>& get_edge_list() { return _edge_list; }
  // setter
  void set_node_list(const std::vector<TopoNode>& node_list) { _node_list = node_list; }
  void set_edge_list(const std::vector<TopoEdge>& edge_list) { _edge_list = edge_list; }
  // function

 private:
  std::vector<TopoNode> _node_list;
  std::vector<TopoEdge> _edge_list;
};

}  // namespace ircx
