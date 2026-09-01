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
#include "PhysicalGraphBuildNode.hpp"
#include "PhysicalGraphBuildTerminal.hpp"

namespace ilvs {

class PhysicalGraphBuildData
{
 public:
  PhysicalGraphBuildData() = default;
  ~PhysicalGraphBuildData() = default;
  // getter
  std::vector<PhysicalGraphBuildNode>& get_graph_node_list() { return _graph_node_list; }
  std::vector<std::pair<int32_t, int32_t>>& get_via_node_pair_list() { return _via_node_pair_list; }
  std::map<std::string, std::vector<PhysicalGraphBuildTerminal>>& get_net_terminal_build_data_map() { return _net_terminal_build_data_map; }
  // const getter
  const std::vector<PhysicalGraphBuildNode>& get_graph_node_list() const { return _graph_node_list; }
  const std::vector<std::pair<int32_t, int32_t>>& get_via_node_pair_list() const { return _via_node_pair_list; }
  const std::map<std::string, std::vector<PhysicalGraphBuildTerminal>>& get_net_terminal_build_data_map() const { return _net_terminal_build_data_map; }

 private:
  std::vector<PhysicalGraphBuildNode> _graph_node_list;
  std::vector<std::pair<int32_t, int32_t>> _via_node_pair_list;
  std::map<std::string, std::vector<PhysicalGraphBuildTerminal>> _net_terminal_build_data_map;
};

}  // namespace ilvs
