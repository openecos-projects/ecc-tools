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

#include "MJHeader.hpp"

namespace imj {

class DFPGConnect
{
 public:
  DFPGConnect() = default;
  ~DFPGConnect() = default;
  // getter
  std::string& get_top_net_name() { return _top_net_name; }
  std::vector<std::string>& get_child_net_name_list() { return _child_net_name_list; }
  // const getter
  const std::string& get_top_net_name() const { return _top_net_name; }
  const std::vector<std::string>& get_child_net_name_list() const { return _child_net_name_list; }
  // setter
  void set_top_net_name(const std::string& top_net_name) { _top_net_name = top_net_name; }
  void set_child_net_name_list(const std::vector<std::string>& child_net_name_list) { _child_net_name_list = child_net_name_list; }
  // function
  bool has_child_net_name(const std::string& child_net_name) const
  {
    return std::find(_child_net_name_list.begin(), _child_net_name_list.end(), child_net_name) != _child_net_name_list.end();
  }

 private:
  std::string _top_net_name;
  std::vector<std::string> _child_net_name_list;
};

}  // namespace imj
