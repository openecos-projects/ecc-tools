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

class DFNetUnion
{
 public:
  DFNetUnion() = default;
  ~DFNetUnion() = default;
  // getter
  std::string get_root_name(const std::string& net_name)
  {
    std::map<std::string, std::string>::iterator iter = _net_name_to_parent_name_map.find(net_name);
    if (iter == _net_name_to_parent_name_map.end()) {
      _net_name_to_parent_name_map[net_name] = net_name;
      return net_name;
    }
    if (iter->second == net_name) {
      return net_name;
    }
    std::string root_name = get_root_name(iter->second);
    iter->second = root_name;
    return root_name;
  }
  // setter
  // function
  std::string merge_net_name(const std::string& target_net_name, const std::string& source_net_name)
  {
    std::string target_root_name = get_root_name(target_net_name);
    std::string source_root_name = get_root_name(source_net_name);
    if (target_root_name != source_root_name) {
      _net_name_to_parent_name_map[source_root_name] = target_root_name;
    }
    return target_root_name;
  }

 private:
  std::map<std::string, std::string> _net_name_to_parent_name_map;
};

}  // namespace imj
