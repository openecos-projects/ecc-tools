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

class PhysicalGraphBuildTerminal
{
 public:
  PhysicalGraphBuildTerminal() = default;
  ~PhysicalGraphBuildTerminal() = default;
  // getter
  std::string& get_terminal_name() { return _terminal_name; }
  std::vector<int32_t>& get_node_idx_list() { return _node_idx_list; }
  // const getter
  const std::string& get_terminal_name() const { return _terminal_name; }
  const std::vector<int32_t>& get_node_idx_list() const { return _node_idx_list; }
  // setter
  void set_terminal_name(const std::string& terminal_name) { _terminal_name = terminal_name; }
  void set_node_idx_list(const std::vector<int32_t>& node_idx_list) { _node_idx_list = node_idx_list; }

 private:
  std::string _terminal_name;
  std::vector<int32_t> _node_idx_list;
};

}  // namespace ilvs
